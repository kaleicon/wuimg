// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2022 kaleido
#include <string.h>

#include <avif/avif.h>

#include "wudefs.h"
#include "misc/bit.h"
#include "misc/common.h"
#include "misc/math.h"
#include "misc/metadata.h"

static void end_avif(struct image_file *infile) {
	avifDecoderDestroy(infile->dec_state);
}

static void get_avif_metadata_item(struct wuimg *img, const avifRWData *meta,
const enum metadata_type type) {
	if (meta->size) {
		struct wutree *tree = wuimg_get_metadata(img);
		if (tree) {
			metadata_parse(type, meta->data, meta->size, tree);
		}
	}
}

static void get_avif_metadata(struct wuimg *img, const avifImage *avif) {
	get_avif_metadata_item(img, &avif->exif, metadata_exif);
	get_avif_metadata_item(img, &avif->xmp, metadata_xmp);
}

static void get_avif_colorspace(struct wuimg *img, const avifImage *avif) {
	if (avif->icc.size) {
		color_space_set_icc_copy(&img->cs, avif->icc.data,
			avif->icc.size);
	} else {
		img->cs.primaries = (enum cicp_primaries)avif->colorPrimaries;
		img->cs.transfer = (enum cicp_transfer)avif->transferCharacteristics;
		img->cs.matrix = (enum cicp_matrix)avif->matrixCoefficients;
		img->cs.limited = avif->yuvRange == AVIF_RANGE_LIMITED;
		if (img->cs.matrix == cicp_matrix_rgb) {
			img->layout = pix_gbra;
		}
	}
}

static void get_avif_transforms(struct wuimg *img, const avifImage *avif) {
	if (avif->transformFlags & AVIF_TRANSFORM_PASP) {
		wuimg_aspect_ratio(img, avif->pasp.hSpacing, avif->pasp.vSpacing);
	}
	// It just so happened that we did everything opposite from AVIF.
	if (avif->transformFlags & AVIF_TRANSFORM_IROT) {
		// AVIF uses counter-clockwise quarter turns.
		img->rotate = 2 ^ avif->irot.angle;
	}
	if (avif->transformFlags & AVIF_TRANSFORM_IMIR) {
		/* AVIF distinguishes between vertical and horizontal
		 * mirroring, and it's applied after rotation. */
		const bool axis =
#if AVIF_VERSION_MAJOR < 1
			avif->imir.mode
#else
			avif->imir.axis
#endif
		;
		img->mirror = true;
		img->rotate ^= (uint8_t)(
			(axis ^ (img->rotate & 1)) << 1
		);
	}
}

static void copy_avif_plane(struct plane_info *p,
const uint8_t *restrict plane_data, const size_t row_bytes) {
	for (size_t y = 0; y < p->h; ++y) {
		const uint8_t *src = plane_data + row_bytes * y;
		uint8_t *dst = p->ptr + p->stride*y;
		memcpy(dst, src, p->stride);
	}
}

static struct wu_st dec_avif_subimg(struct image_file *infile,
struct wuimg *img, const uint32_t idx) {
	avifDecoder *dec = infile->dec_state;

	const avifResult res = avifDecoderNthImage(dec, idx);
	if (res != AVIF_RESULT_OK) {
		image_file_strerror_append(infile, dec->diag.error);
		return WUERR_HERE(wu_decoding_error);
	}

	avifImage *avif = dec->image;
	avifPixelFormatInfo info;
	avifGetPixelFormatInfo(avif->yuvFormat, &info);
	const uint8_t colors = info.monochrome ? 1 : 3;
	img->w = avif->width;
	img->h = avif->height;
	img->channels = (uint8_t)(colors + dec->alphaPresent);
	img->bitdepth = (avif->depth > 8) ? 16 : 8;
	img->bitrange = (uint8_t)avif->depth;
	img->alpha = avif->alphaPremultiplied
		? alpha_associated : alpha_unassociated;

	get_avif_colorspace(img, avif);
	get_avif_transforms(img, avif);
	get_avif_metadata(img, avif);

	struct image_planes *planes = wuimg_plane_init(img);
	if (!planes) {
		return WUERR_HERE(wu_alloc_error);
	}
	switch (avif->yuvFormat) {
	case AVIF_PIXEL_FORMAT_YUV422: wuimg_plane_subsamp(img, 2, 1); break;
	case AVIF_PIXEL_FORMAT_YUV420: wuimg_plane_subsamp(img, 2, 2); break;
	default: break;
	}
	switch (avif->yuvChromaSamplePosition) {
	case AVIF_CHROMA_SAMPLE_POSITION_UNKNOWN:
	case AVIF_CHROMA_SAMPLE_POSITION_RESERVED:
		break;
	case AVIF_CHROMA_SAMPLE_POSITION_VERTICAL:
		wuimg_plane_cosit(img, true, false);
		break;
	case AVIF_CHROMA_SAMPLE_POSITION_COLOCATED:
		wuimg_plane_cosit(img, true, true);
		break;
	}

	/* libavif's memory layout is weird in all sorts of ways, so we need to
	 * do a full memcpy */
	const enum wu_error err = wuimg_alloc_limit(img, infile->conf);
	if (err != wu_ok) {
		return WUERR_HERE(err);
	}
	uint8_t z = 0;
	while (z < colors) {
		copy_avif_plane(planes->p + z, avif->yuvPlanes[z],
			avif->yuvRowBytes[z]);
		++z;
	}
	if (avif->alphaPlane) {
		copy_avif_plane(planes->p + z, avif->alphaPlane,
			avif->alphaRowBytes);
	}
	return WU_OK;
}

static struct wu_st event_avif(struct image_file *infile,
struct wu_state *state, const enum image_event ev) {
	const uint32_t idx = (uint32_t)state->idx;
	return (ev == ev_subcycle)
		? dec_avif_subimg(infile, infile->sub_img + idx, idx)
		: WU_NO_CHANGE;
}

static struct wu_st decode_avif_from_map(struct image_file *infile,
avifDecoder *dec, avifResult *res) {
	dec->strictFlags = AVIF_STRICT_DISABLED;
	dec->maxThreads = (int)num_cpus();
	*res = avifDecoderSetIOMemory(dec, infile->map.ptr, infile->map.len);
	if (*res == AVIF_RESULT_OK) {
		*res = avifDecoderParse(dec);
		if (*res == AVIF_RESULT_OK) {
			return alloc_sub_images(infile, (size_t)dec->imageCount)
				? WU_OK : WUERR_HERE(wu_alloc_error);
		}
	}
	return WUERR_HERE(wu_invalid_header);
}

static struct wu_st init_avif(struct image_file *infile) {
	avifDecoder *dec = avifDecoderCreate();
	if (dec) {
		infile->dec_state = dec;
		avifResult res;
		const struct wu_st st = decode_avif_from_map(infile, dec, &res);
		if (res != AVIF_RESULT_OK) {
			image_file_strerror_append(infile, dec->diag.error);
		}
		return st;
	}
	return WUERR_HERE(wu_alloc_error);
}

const struct image_fn avif_fn = {
	.mmap = true,
	.init = init_avif,
	.event = event_avif,
	.end = end_avif,
};

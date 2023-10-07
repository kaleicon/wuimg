// SPDX-License-Identifier: 0BSD
#include <avif/avif.h>

#include "wudefs.h"
#include "metadata.h"
#include "misc/bit.h"
#include "misc/common.h"
#include "misc/math.h"

struct avif_state {
	avifDecoder *dec;
	uint32_t idx;
};

static void clean_avif_state(struct image_file *infile) {
	struct avif_state *ds = infile->dec_state;
	if (ds->dec) {
		avifDecoderDestroy(ds->dec);
	}
}

static void read_metadata_item(struct wuimg *img, const avifRWData *meta,
const enum metadata_type type) {
	if (meta->size) {
		struct wu_tree *tree = wuimg_get_metadata(img);
		if (tree) {
			standard_metadata(type, meta->data, meta->size, tree);
		}
	}
}

static void get_metadata(struct wuimg *img, const avifImage *avif) {
	read_metadata_item(img, &avif->exif, exif_metadata);
	read_metadata_item(img, &avif->xmp, xmp_metadata);
}

static void get_colorspace(struct wuimg *img, const avifImage *avif) {
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

static void get_transforms(struct wuimg *img, const avifImage *avif) {
	if (avif->transformFlags & AVIF_TRANSFORM_PASP) {
		img->ratio = (float)avif->pasp.hSpacing / (float)avif->pasp.vSpacing;
	}
	// It just so happened that we did everything opposite from AVIF.
	if (avif->transformFlags & AVIF_TRANSFORM_IROT) {
		// AVIF uses counter-clockwise quarter turns.
		img->rotate = 2 ^ avif->irot.angle;
	}
	if (avif->transformFlags & AVIF_TRANSFORM_IMIR) {
		/* AVIF distinguishes between vertical and horizontal
		 * mirroring, and it's applied after rotation. */
		img->mirror = true;
		img->rotate ^= (uint8_t)((avif->imir.mode ^ (img->rotate & 1)) << 1);
	}
}

static enum wu_error dec_subimg(struct image_file *infile,
const struct wu_conf *wuconf, struct wuimg *img, const uint32_t idx,
struct avif_state *ds) {
	avifDecoder *dec = ds->dec;

	const avifResult res = avifDecoderNthImage(dec, idx);
	if (res != AVIF_RESULT_OK) {
		image_file_strerror_append(infile, dec->diag.error);
		return wu_decoding_error;
	}

	avifImage *avif = dec->image;
	img->w = avif->width;
	img->h = avif->height;
	if (wuimg_exceeds_limit(img, wuconf)) {
		return wu_exceeds_size_limit;
	}

	avifPixelFormatInfo info;
	avifGetPixelFormatInfo(avif->yuvFormat, &info);
	const uint8_t colors = info.monochrome ? 1 : 3;
	img->channels = (uint8_t)(colors + dec->alphaPresent);
	img->bitdepth = (avif->depth > 8) ? 16 : 8;
	img->align_sh = 2;
	img->alpha = avif->alphaPremultiplied
		? alpha_associated : alpha_unassociated;

	get_colorspace(img, avif);
	get_transforms(img, avif);
	get_metadata(img, avif);

	struct image_planes *planes = wuimg_plane_init(img);
	if (!planes) {
		return wu_alloc_error;
	}
	switch (avif->yuvFormat) {
	case AVIF_PIXEL_FORMAT_YUV422: wuimg_plane_subsamp(img, 2, 1); break;
	case AVIF_PIXEL_FORMAT_YUV420: wuimg_plane_subsamp(img, 2, 2); break;
	default: break;
	}
	switch (avif->yuvChromaSamplePosition) {
	case AVIF_CHROMA_SAMPLE_POSITION_UNKNOWN:
		break;
	case AVIF_CHROMA_SAMPLE_POSITION_VERTICAL:
		wuimg_plane_position(img, -1, 0);
		break;
	case AVIF_CHROMA_SAMPLE_POSITION_COLOCATED:
		wuimg_plane_position(img, -1, -1);
		break;
	}

	/* libavif's memory layout is weird in all sorts of ways, so we need to
	 * do a full memcpy */
	const enum wu_error st = wuimg_alloc(img);
	if (st != wu_ok) {
		return st;
	}

	struct wuptr src[4];
	uint8_t i = 0;
	while (i < colors) {
		src[i] = wuptr_mem(avif->yuvPlanes[i], avif->yuvRowBytes[i]);
		++i;
	}
	if (avif->alphaPlane) {
		src[i] = wuptr_mem(avif->alphaPlane, avif->alphaRowBytes);
	}

	const struct scale_info si = strip_scale_info(
		bit_set32((uint32_t)avif->depth), img->bitdepth);
	for (uint8_t z = 0; z < img->channels; ++z) {
		struct plane_info *p = planes->p + z;
		for (size_t y = 0; y < p->h; ++y) {
			strip_scale(p->ptr + p->stride*y,
				src[z].ptr + src[z].len*y, p->w, si, pix_normal);
		}
	}
	return wu_ok;
}

static enum wu_error avif_callback(struct image_file *infile,
const struct wu_conf *wuconf, struct wu_state *state, const enum image_event ev) {
	if (ev) {
		const uint32_t idx = (uint32_t)state->idx;
		struct wuimg *img = infile->sub_img + idx;
		struct avif_state *ds = infile->dec_state;
		return dec_subimg(infile, wuconf, img, idx, ds);
	} else {
		clean_avif_state(infile);
	}
	return wu_no_change;
}

static enum wu_error decode_map(struct image_file *infile,
struct avif_state *ds, avifResult *res) {
	avifDecoder *dec = ds->dec;
	//dec->strictFlags = AVIF_STRICT_DISABLED;
	dec->maxThreads = (int)num_cpus();
	*res = avifDecoderSetIOMemory(dec, infile->map.data, infile->map.len);
	if (*res != AVIF_RESULT_OK) {
		return wu_invalid_header;
	}
	*res = avifDecoderParse(dec);
	if (*res != AVIF_RESULT_OK) {
		return wu_invalid_header;
	}
	return alloc_sub_images(infile, (size_t)dec->imageCount)
		? wu_ok : wu_alloc_error;
}

static enum wu_error avif_dec(struct image_file *infile,
const struct wu_conf *wuconf) {
	(void)wuconf;
	struct avif_state *ds = calloc(1, sizeof(*ds));
	if (ds) {
		ds->dec = avifDecoderCreate();
		if (ds->dec) {
			infile->dec_state = ds;
			infile->events = ev_subcycle;
			avifResult res;
			const enum wu_error st = decode_map(infile, ds,
				&res);
			if (res != AVIF_RESULT_OK) {
				image_file_strerror_append(infile,
					ds->dec->diag.error);
			}
			return st;
		}
		free(ds);
	}
	return wu_alloc_error;
}

const struct image_fn avif_fn = {.mmap = true,
	.dec = avif_dec, .callback = avif_callback};

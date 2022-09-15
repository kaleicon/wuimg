#include <avif/avif.h>

#include "wudefs.h"
#include "metadata.h"
#include "common/file.h"

struct avif_state {
	struct map_info mm;
	avifDecoder *dec;
	uint32_t idx;
};

static void clean_avif_state(struct image_file *infile) {
	struct avif_state *ds = infile->dec_state;
	if (ds->dec) {
		avifDecoderDestroy(ds->dec);
	}
	file_unmap(&ds->mm);
}

static void read_metadata_item(struct raw_img *img, avifRWData *meta,
const enum metadata_type type) {
	if (meta->size) {
		struct wu_tree *tree = raw_img_get_metadata(img);
		if (tree) {
			standard_metadata(type, meta->data, meta->size, tree);
		}
	}
}

static void get_metadata(struct raw_img *img, avifImage *avif) {
	read_metadata_item(img, &avif->exif, exif_metadata);
	read_metadata_item(img, &avif->xmp, xmp_metadata);
}

static void get_colorspace(struct raw_img *img, avifImage *avif) {
	if (avif->icc.size) {
		color_space_set_icc_copy(&img->cs, avif->icc.data,
			avif->icc.size);
	} else {
		img->cs.primaries = (enum cicp_primaries)avif->colorPrimaries;
		img->cs.transfer = (enum cicp_transfer)avif->transferCharacteristics;
		img->cs.matrix = (enum cicp_matrix)avif->matrixCoefficients;
		img->cs.limited = avif->yuvRange == AVIF_RANGE_LIMITED;
	}
}

static void get_transforms(struct raw_img *img, avifImage *avif) {
	if (avif->transformFlags & AVIF_TRANSFORM_PASP) {
		img->ratio = (float)avif->pasp.hSpacing / (float)avif->pasp.vSpacing;
	}
	if (avif->transformFlags & AVIF_TRANSFORM_IROT) {
		img->rotate = avif->irot.angle;
	}
	if (avif->transformFlags & AVIF_TRANSFORM_IMIR) {
		img->mirror = true;
		img->rotate ^= avif->imir.mode << 1;
	}
}

static enum wu_error dec_subimg(struct image_file *infile,
const struct wu_conf *wuconf, const uint32_t idx) {
	struct raw_img *img = infile->sub_img + idx;
	struct avif_state *ds = infile->dec_state;
	avifDecoder *dec = ds->dec;

	const avifResult res = avifDecoderNthImage(dec, idx);
	if (res != AVIF_RESULT_OK) {
		image_file_error_append(infile, avifResultToString(res));
		return wu_decoding_error;
	}

	avifImage *avif = dec->image;
	struct image_planes *planes = img->u.planes;
	if (!planes) {
		img->w = avif->width;
		img->h = avif->height;
		if (raw_img_exceeds_limit(img, wuconf)) {
			return wu_exceeds_size_limit;
		}

		avifPixelFormatInfo info;
		avifGetPixelFormatInfo(avif->yuvFormat, &info);
		img->channels = (uint8_t)((info.monochrome ? 1 : 3) + dec->alphaPresent);
		img->bitdepth = (avif->depth > 8) ? 16 : 8;
		img->used_bits = (uint8_t)avif->depth;
		img->alpha = avif->alphaPremultiplied ? alpha_associated : alpha_unassociated;
		img->align_sh = strip_alignment(avif->yuvRowBytes[0], img->w,
			img->bitdepth);
		if (img->align_sh < 0) {
			return wu_invalid_params;
		}
		get_transforms(img, avif);
		get_colorspace(img, avif);
		get_metadata(img, avif);

		planes = raw_img_plane_init(img);
		if (!planes) {
			return wu_alloc_error;
		}
		switch (avif->yuvFormat) {
		case AVIF_PIXEL_FORMAT_YUV422:
			raw_img_plane_subsamp(img, 2, 1);
			break;
		case AVIF_PIXEL_FORMAT_YUV420:
			raw_img_plane_subsamp(img, 2, 2);
			break;
		default: break;
		}
		raw_img_plane_resolve(img);
		const enum wu_error st = raw_img_verify(img);
		if (st != wu_ok) {
			return st;
		}
		img->borrowed = true;
	}
	switch (img->channels) {
	case 4: planes->p[3].ptr = avif->alphaPlane; // fallthrough
	case 3: planes->p[2].ptr = avif->yuvPlanes[2]; // fallthrough
	case 2: planes->p[1].ptr = avif->yuvPlanes[1]; // fallthrough
	case 1: planes->p[0].ptr = avif->yuvPlanes[0]; // fallthrough
	}
	return wu_ok;
}

enum wu_error avif_callback(struct image_file *infile,
const struct wu_conf *wuconf, struct wu_state *state, const enum image_event ev) {
	if (ev == ev_subcycle) {
		return dec_subimg(infile, wuconf, (uint32_t)state->idx);
	}
	clean_avif_state(infile);
	return wu_ok;
}

static enum wu_error decode_map(struct image_file *infile,
const struct wu_conf *wuconf, struct avif_state *ds, avifResult *res) {
	avifDecoder *dec = ds->dec;
	*res = avifDecoderSetIOMemory(dec, ds->mm.data, ds->mm.len);
	if (*res != AVIF_RESULT_OK) {
		return wu_invalid_header;
	}

	*res = avifDecoderParse(dec);
	if (*res != AVIF_RESULT_OK) {
		return wu_invalid_header;
	}

	struct raw_img *img = alloc_sub_images(infile, (size_t)dec->imageCount);
	if (!img) {
		return wu_alloc_error;
	}
	return dec_subimg(infile, wuconf, 0);
}

enum wu_error avif_dec(struct image_file *infile, const struct wu_conf *wuconf) {
	struct avif_state *ds = calloc(1, sizeof(*ds));
	if (ds) {
		infile->dec_state = ds;
		if (file_map(&ds->mm, infile->ifp)) {
			ds->dec = avifDecoderCreate();
			if (ds->dec) {
				avifResult res;
				const enum wu_error st = decode_map(infile,
					wuconf, ds, &res);
				if (res != AVIF_RESULT_OK) {
					image_file_error_append(infile,
						avifResultToString(res));
				}
				if (st == wu_ok) {
					infile->events = ev_subcycle;
				}
				return st;
			}
		}
	}
	return wu_alloc_error;
}

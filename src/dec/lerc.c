// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2022 kaleido
#include <Lerc_c_api.h>

#include "misc/common.h"
#include "misc/math.h"
#include "wudefs.h"

static enum wu_error map_lerc_to_wu(const lerc_status status,
const enum wu_error fallback_fail) {
	switch (status) {
	case 0: return wu_ok;
	case 1: return fallback_fail;
	case 2: return wu_invalid_params;
	}
	return wu_unknown_error;
}

static struct wu_st set_lerc_mask(struct wuimg *img, const struct wu_conf *conf,
const unsigned w, const unsigned h, const unsigned mask_nb) {
	img->w = w;
	img->h = h;
	img->channels = (unsigned char)mask_nb;
	img->bitdepth = 8;
	img->bitrange = 1;
	if (wuimg_plane_init(img)) {
		return WUERR_CHECK(wuimg_alloc_limit(img, conf));
	}
	return WUERR_HERE(wu_alloc_error);
}

static struct wu_st set_lerc_main(struct wuimg *img, const struct wu_conf *conf,
const unsigned w, const unsigned h, const unsigned dims, const unsigned bands,
const unsigned type) {
	img->w = w;
	img->h = h;
	img->channels = (unsigned char)(dims * bands);
	switch (type) {
	case 0: case 1: // char/uchar
	case 2: case 3: // short/ushort
	case 4: case 5: // int/uint
		img->bitdepth = (uint8_t)(8 << (type / 2));
		img->attr = (type & 1) ? pix_normal : pix_signed;
		break;
	case 6: case 7: // float/double
		img->bitdepth = (type & 1) ? 64 : 32;
		img->attr = pix_float;
		break;
	default:
		return WUERR_HERE(wu_unsupported_feature);
	}

	if (bands > 1 && !wuimg_plane_init(img)) {
		return WUERR_HERE(wu_alloc_error);
	}
	return WUERR_CHECK(wuimg_alloc_limit(img, conf));
}

static struct wu_st init_lerc(struct image_file *infile) {
	const struct wuptr *mm = &infile->map;
	unsigned info[9];
	enum wu_error err = map_lerc_to_wu(lerc_getBlobInfo(mm->ptr,
		(unsigned)mm->len, info, NULL, ARRAY_LEN(info), 0),
		wu_invalid_header);
	if (err != wu_ok) {
		return wuerr(err, "failed to get blob info");
	}

	const unsigned w = info[3];
	const unsigned h = info[4];
	const unsigned dims = info[2];
	const unsigned bands = info[5];
	const unsigned mask_nb = info[8];
	if ((dims != 1 && bands != 1) || umax(dims, umax(bands, mask_nb)) > 4) {
		return WUERR_HERE(wu_unsupported_feature);
	}

	struct wuimg *img = alloc_sub_images(infile, 1 + (bool)mask_nb);
	if (!img) {
		return WUERR_HERE(wu_alloc_error);
	}

	const struct wu_conf *conf = infile->conf;
	const unsigned type = info[1];
	struct wu_st st = set_lerc_main(img, conf, w, h, dims, bands, type);
	if (!wu_isok(st)) {
		return st;
	}

	uint8_t *mask = NULL;
	if (mask_nb) {
		st = set_lerc_mask(img + 1, conf, w, h, mask_nb);
		if (!wu_isok(st)) {
			return st;
		}
		mask = img[1].data;
	}

	return WUERR_HERE(map_lerc_to_wu(
		lerc_decode(mm->ptr, (unsigned)mm->len, (int)mask_nb, mask,
			(int)dims, (int)w, (int)h, (int)bands, type,
			img[0].data),
		wu_decoding_error
	));
}

const struct image_fn lerc_fn = {
	.mmap = true,
	.init = init_lerc,
};

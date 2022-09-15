#include <Lerc_c_api.h>

#include "rast_utils.h"
#include "common/common.h"
#include "common/math.h"

static enum wu_error map_lerc_to_wu(const lerc_status status,
const enum wu_error fallback_fail) {
	switch (status) {
	case 0: return wu_ok;
	case 1: return fallback_fail;
	case 2: return wu_invalid_params;
	}
	return wu_unknown_error;
}

static enum wu_error set_mask(struct raw_img *img, const unsigned w,
const unsigned h, const unsigned mask_nb) {
	img->w = w;
	img->h = h;
	img->channels = (unsigned char)mask_nb;
	img->bitdepth = 8;
	if (raw_img_plane_init(img)) {
		return raw_img_alloc(img);
	}
	return wu_alloc_error;
}

static enum wu_error set_main(struct raw_img *img, const unsigned w,
const unsigned h, const unsigned dims, const unsigned bands,
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
		return wu_unsupported_feature;
	}

	if (bands > 1 && !raw_img_plane_init(img)) {
		return wu_alloc_error;
	}
	return raw_img_alloc(img);
}

static enum wu_error dec_wrap(struct image_file *infile,
const struct wu_conf *wuconf, const struct map_info *mm) {
	unsigned info[9];
	enum wu_error err = map_lerc_to_wu(lerc_getBlobInfo(mm->data,
		(unsigned)mm->len, info, NULL, ARRAY_LEN(info), 0),
		wu_invalid_header);
	if (err != wu_ok) {
		image_file_error_append(infile, "Failed to get blob info");
		return wu_invalid_header;
	}

	const unsigned w = info[3];
	const unsigned h = info[4];
	if (umax(w, h) > wuconf->max_img_size) {
		return wu_exceeds_size_limit;
	}

	const unsigned dims = info[2];
	const unsigned bands = info[5];
	const unsigned mask_nb = info[8];
	if ((dims != 1 && bands != 1) || umax(dims, umax(bands, mask_nb)) > 4) {
		return wu_unsupported_feature;
	}

	struct raw_img *img = alloc_sub_images(infile, 1 + (bool)mask_nb);
	if (!img) {
		return wu_alloc_error;
	}

	const unsigned type = info[1];
	err = set_main(img, w, h, dims, bands, type);
	if (err != wu_ok) {
		return err;
	}

	uint8_t *mask = NULL;
	if (mask_nb) {
		err = set_mask(img + 1, w, h, mask_nb);
		if (err != wu_ok) {
			return err;
		}
		mask = img[1].data;
	}

	return map_lerc_to_wu(
		lerc_decode(mm->data, (unsigned)mm->len, (int)mask_nb, mask,
			(int)dims, (int)w, (int)h, (int)bands, type,
			img[0].data),
		wu_decoding_error
	);
}

enum wu_error lerc_dec(struct image_file *infile, const struct wu_conf *wuconf) {
	return rast_map_wrap(infile, wuconf, dec_wrap);
}

#include "wudefs.h"
#include "rast_utils.h"

#include <Lerc_c_api.h>

static enum wu_error map_lerc_to_wu(const lerc_status status,
const enum wu_error fallback_fail) {
	switch (status) {
	case 0: return wu_ok;
	case 1: return fallback_fail;
	case 2: return wu_invalid_params;
	}
	return wu_unknown_error;
}

static enum wu_error dec_and_mask(struct raw_img *img,
const struct map_info *mm, const unsigned bands, const unsigned dim,
const unsigned type, const unsigned mask_nb) {
	const size_t mask_dims = img->w * img->h;
	unsigned char *mask = malloc(mask_dims * mask_nb);
	const enum wu_error err = map_lerc_to_wu(
		lerc_decode(mm->data, (unsigned)mm->len, (int)mask_nb, mask,
			(int)dim, (int)img->w, (int)img->h, (int)bands,
			type, img->data),
		wu_decoding_error
	);
	free(mask); // TODO: Apply the mask
	return err;
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

	const unsigned dim = info[2];
	const unsigned bands = info[5];
	if ((dim != 1 && bands != 1) || dim > 4 || bands > 4) {
		return wu_unsupported_feature;
	}

	struct raw_img *img = alloc_sub_images(infile, 1);
	if (!img) {
		return wu_alloc_error;
	}

	img->w = info[3];
	img->h = info[4];
	img->channels = (unsigned char)(dim * bands);
	if (raw_img_exceeds_limit(img, wuconf)) {
		return wu_exceeds_size_limit;
	}
	switch (info[1]) {
	case 0: case 1: // char/uchar
	case 2: case 3: // short/ushort
	case 4: case 5: // int/uint
		img->bitdepth = 8 << (info[1] >> 1);
		img->attr = (info[1] & 1) ? pix_normal : pix_signed;
		break;
	case 6: case 7: // float/double
		img->bitdepth = (info[1] & 1) ? 64 : 32;
		img->attr = pix_float;
		break;
	default:
		return wu_unsupported_feature;
	}

	if (bands > 1 && !raw_img_plane_init(img)) {
		return wu_alloc_error;
	}
	const enum wu_error status = raw_img_alloc(img);
	if (status == wu_ok) {
		return dec_and_mask(img, mm, bands, dim, info[1], info[8]);
	}
	return status;
}

enum wu_error lerc_dec(struct image_file *infile, const struct wu_conf *wuconf) {
	return rast_map_wrap(infile, wuconf, dec_wrap);
}

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <limits.h>

#include "../wudefs.h"
#include "../common.h"
#include "lib/pnm.h"

enum wu_error pnm_dec(struct image_file *infile,
const struct wu_conf *wuconf) {
	struct pnm_desc desc;
	enum lib_fail fail = pnm_open_file(infile->ifp, &desc, true);
	if (fail) {
		infile->err_msg = strdup(lib_fail_string(fail));
		return wu_open_error;
	}

	fail = pnm_parse_header(&desc);
	if (fail) {
		infile->err_msg = strdup(lib_fail_string(fail));
		return wu_invalid_header;
	}

	if (desc.ch > 4) {
		return wu_unsupported_feature;
	} else if (zumax(desc.w, desc.h) > wuconf->max_img_size) {
		return wu_exceeds_size_limit;
	}

	struct raw_img *img = alloc_sub_images(infile, zumin(desc.nr, UCHAR_MAX));
	if (!img) {
		return wu_alloc_error;
	}

	desc.expand = false;
	size_t i = 0;
	do {
		img[i].w = desc.w;
		img[i].h = desc.h;
		img[i].channels = desc.ch;
		img[i].bitdepth = (unsigned char)(desc.bytedepth * 8);
		switch (desc.type) {
		case xv_thumb:
			if (!desc.expand) {
				img[i].bitdepth = rgb332;
			}
			break;
		case raw_pbm:
			if (!desc.expand) {
				img[i].bitdepth = 1;
				img[i].attr |= pix_inverted;
			}
			break;
		case color_pfm:
		case gray_pfm:
			img[i].mirror = true;
			img[i].attr |= pix_float;
			break;
		default:
			break;
		}

		img[i].data = pnm_decode_next(&desc);
		if (!img[i].data) {
			break;
		}
		++i;
	} while (i < infile->nr);

	if (!i) {
		return wu_decoding_error;
	} else if (i < infile->nr) {
		realloc_sub_images(infile, i);
	}
	return wu_ok;
}

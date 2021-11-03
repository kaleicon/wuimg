#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <limits.h>

#include "../wudefs.h"
#include "../common.h"
#include "../rast_utils.h"
#include "../lib/pnm.h"

enum wu_error pnm_dec(struct image_file *infile,
const struct wu_conf *wuconf) {
	struct pnm_desc desc;
	enum lib_fail fail = pnm_open_file(infile->ifp, &desc, true);
	if (fail) {
		rast_error(infile, fail);
		return wu_open_error;
	}

	fail = pnm_parse_header(&desc);
	if (fail) {
		rast_error(infile, fail);
		return wu_invalid_header;
	}

	if (desc.rast.ch > 4) {
		return wu_unsupported_feature;
	} else if (rast_exceeds_size(&desc.rast, wuconf)) {
		return wu_exceeds_size_limit;
	}

	struct raw_img *img = alloc_sub_images(infile, zumin(desc.nr, UCHAR_MAX));
	if (!img) {
		return wu_alloc_error;
	}

	size_t i = 0;
	do {
		img[i].data = pnm_decode_next(&desc);
		if (!img[i].data) {
			break;
		}

		rast_to_raw(img + i, &desc.rast);
		switch (desc.type) {
		case color_pfm:
		case gray_pfm:
			img[i].mirror = true;
			break;
		default:
			break;
		}

		++i;
	} while (i < infile->nr);
	return image_file_total_decoded(infile, i);
}

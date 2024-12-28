// SPDX-License-Identifier: 0BSD
#include "wudefs.h"
#include "lib/signum.h"

static enum wu_error imc_dec(struct image_file *infile,
const struct wu_conf *conf) {
	struct imc_desc desc;
	enum wu_error st = imc_identify(&desc, infile->map);
	if (st == wu_ok) {
		struct wuimg *img = infile->sub_img;
		st = imc_parse(&desc, img);
		if (st == wu_ok) {
			if (!wuimg_exceeds_limit(img, conf)) {
				return imc_decode(&desc, img)
					? wu_ok : wu_decoding_error;
			}
			return wu_exceeds_size_limit;
		}
	}
	return st;
}

const struct image_fn imc_fn = {
	.mmap = true,
	.alloc_single = true,
	.dec = imc_dec,
};

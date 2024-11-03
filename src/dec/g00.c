// SPDX-License-Identifier: 0BSD
#include "lib/g00.h"
#include "wudefs.h"

static void g00_end(struct image_file *infile) {
	g00_cleanup(infile->dec_state, infile->sub_img);
}

static enum wu_error g00_dec(struct image_file *infile,
const struct wu_conf *wuconf) {
	struct g00_desc *desc = infile->dec_state;
	struct wuimg *img = infile->sub_img;
	enum wu_error st = g00_read_header(desc, img, infile->ifp);
	if (st == wu_ok) {
		tree_bud_leaf_u(&infile->metadata, "Version", desc->version);
		if (wuimg_exceeds_limit(img, wuconf)) {
			st = wu_exceeds_size_limit;
		} else {
			st = g00_decode(desc, img) ? wu_ok : wu_decoding_error;
		}
	}
	return st;
}

const struct image_fn g00_fn = {
	.alloc_single = true,
	.state_size = sizeof(struct g00_desc),
	.dec = g00_dec,
	.end = g00_end,
};

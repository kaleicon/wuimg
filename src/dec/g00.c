// SPDX-License-Identifier: 0BSD
#include "lib/g00.h"
#include "wudefs.h"

static void g00_end(struct image_file *infile) {
	g00_cleanup(infile->dec_state, infile->sub_img);
	free(infile->dec_state);
}

static enum wu_error decode(struct image_file *infile,
const struct wu_conf *wuconf, struct g00_desc *desc) {
	struct wuimg *img = infile->sub_img;
	const enum wu_error st = g00_read_header(desc, img, infile->ifp);
	if (st != wu_ok) {
		return st;
	}

	if (wuimg_exceeds_limit(img, wuconf)) {
		return wu_exceeds_size_limit;
	}

	tree_bud_leaf_u(&infile->metadata, "Version", desc->version);
	return g00_decode(desc, img) ? wu_ok : wu_decoding_error;
}

static enum wu_error g00_dec(struct image_file *infile,
const struct wu_conf *wuconf) {
	struct g00_desc *desc = malloc(sizeof(*desc));
	if (desc) {
		const enum wu_error st = decode(infile, wuconf, desc);
		if (st == wu_ok) {
			infile->dec_state = desc;
		} else {
			g00_cleanup(desc, infile->sub_img);
			free(desc);
		}
		return st;
	}
	return wu_alloc_error;
}

const struct image_fn g00_fn = {
	.alloc_single = true,
	.dec = g00_dec,
	.end = g00_end,
};

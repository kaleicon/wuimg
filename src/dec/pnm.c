// SPDX-License-Identifier: 0BSD
#include <limits.h>

#include "wudefs.h"
#include "lib/pnm.h"

static void pnm_end(struct image_file *infile) {
	free(infile->dec_state);
}

static enum wu_error pnm_callback(struct image_file *infile,
const struct wu_conf *wuconf, struct wu_state *state, const enum image_event ev) {
	(void)wuconf; (void)ev;
	const size_t i = (size_t)state->idx;
	struct wuimg *img = infile->sub_img + i;
	return pnm_decode(infile->dec_state, img, i)
		? wu_ok : wu_decoding_error;
}

static enum wu_error pnm_dec(struct image_file *infile,
const struct wu_conf *wuconf) {
	struct pnm_desc *desc = malloc(sizeof(*desc));
	if (desc) {
		infile->dec_state = desc;
		infile->events = ev_subcycle;
		enum wu_error st = pnm_open_file(desc, infile->ifp, true);
		if (st == wu_ok) {
			st = pnm_parse_header(desc);
			if (st == wu_ok) {
				tree_add_leaf_utf8(&infile->metadata, "Type",
					pnm_type_str(desc->type));
				if (!wuimg_exceeds_limit(&desc->rast, wuconf)) {
					return alloc_sub_images(infile, desc->nr)
						? wu_ok : wu_alloc_error;
				}
				return wu_exceeds_size_limit;
			}
		}
		return st;
	}
	return wu_alloc_error;
}

const struct image_fn pnm_fn = {
	.dec = pnm_dec,
	.callback = pnm_callback,
	.end = pnm_end,
};

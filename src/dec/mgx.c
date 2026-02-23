// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2025 kaleido
#include "lib/mgx.h"
#include "wudefs.h"

static struct wu_st event_mgxicn(struct image_file *infile,
struct wu_state *state, const enum image_event ev) {
	struct wu_st st = WU_NO_CHANGE;
	if (ev == ev_subcycle) {
		struct mgxicn_desc *desc = infile->dec_state;
		const uint32_t i = (uint32_t)state->idx;
		while (desc->idx <= i) {
			struct wuimg *img = infile->sub_img + desc->idx;
			st = mgxicn_set_next(desc, img);
			if (!wu_isok(st)) {
				break;
			}
			enum wu_error err = wuimg_alloc_limit(img, infile->conf);
			if (err != wu_ok) {
				st = WUERR_HERE(err);
				break;
			}
			st = mgxicn_load(desc, img);
			if (!wu_isok(st)) {
				break;
			}
		}
	}
	return st;
}

static struct wu_st init_mgxicn(struct image_file *infile) {
	struct mgxicn_desc *desc = infile->dec_state;
	struct wu_st st = mgxicn_init(desc, infile->ifp);
	infile->nr = desc->nr;
	return st;
}

const struct image_fn mgxicn_fn = {
	.state_size = sizeof(struct mgxicn_desc),
	.init = init_mgxicn,
	.event = event_mgxicn,
};

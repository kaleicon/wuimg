// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2026 kaleido
#include "lib/kaboom.h"
#include "wudefs.h"

static void end_bmb(struct image_file *infile) {
	bmb_cleanup(infile->dec_state);
}

static struct wu_st event_bmb(struct image_file *infile,
const struct wu_conf *conf, struct wu_state *state, const enum image_event ev) {
	struct wu_st st = WU_NO_CHANGE;
	struct bmb_desc *desc = infile->dec_state;
	if (ev == ev_subcycle) {
		while (desc->i <= (uint32_t)state->idx) {
			struct wuimg *img = infile->sub_img + desc->i;
			st = bmb_parse_next(desc, img);
			if (!wu_isok(st)) {
				break;
			}
			enum wu_error e = wuimg_alloc_limit(img, conf);
			if (e != wu_ok) {
				st = WUERR_HERE(e);
				break;
			}
			st = bmb_decode(desc, img);
			if (!wu_isok(st)) {
				break;
			}
		}
	}
	return st;
}

static struct wu_st init_bmb(struct image_file *infile,
const struct wu_conf *conf) {
	(void)conf;
	struct bmb_desc *desc = infile->dec_state;
	struct wu_st st = bmb_init(desc, infile->ifp);
	infile->nr = desc->nr;
	return st;
}

const struct image_fn bmb_fn = {
	.state_size = sizeof(struct bmb_desc),
	.init = init_bmb,
	.event = event_bmb,
	.end = end_bmb,
};

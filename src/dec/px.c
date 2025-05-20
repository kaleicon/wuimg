// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2022 kaleido
#include "wudefs.h"
#include "misc/math.h"
#include "lib/px.h"

static enum wu_error px_callback(struct image_file *infile,
const struct wu_conf *wuconf, struct wu_state *state, const enum image_event ev) {
	(void)wuconf;
	const uint32_t idx = (uint32_t)state->idx;
	struct wuimg *img = infile->sub_img + idx;
	return (ev == ev_subcycle)
		? px_decode(infile->dec_state, img, idx)
		: wu_no_change;
}

static enum wu_error px_dec(struct image_file *infile,
const struct wu_conf *wuconf) {
	struct px_desc *desc = infile->dec_state;
	const enum wu_error err = px_parse(desc, infile->ifp);
	if (err == wu_ok) {
		if (umax(desc->w, desc->h) > wuconf->max_img_size) {
			return wu_exceeds_size_limit;
		}
		return alloc_sub_images(infile, desc->nr)
			? wu_ok : wu_alloc_error;
	}
	return err;
}

const struct image_fn px_fn = {
	.state_size = sizeof(struct px_desc),
	.dec = px_dec,
	.callback = px_callback,
};

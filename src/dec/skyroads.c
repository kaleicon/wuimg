// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2025 kaleido
#include "wudefs.h"
#include "lib/skyroads.h"

static enum wu_error skyroads_callback(struct image_file *infile,
const struct wu_conf *_c, struct wu_state *_s, const enum image_event ev) {
	(void)_c; (void)_s;
	if (ev == ev_subcycle) {
		const struct mparser *mp = infile->dec_state;
		return skyroads_decode(*mp, infile->sub_img)
			? wu_ok : wu_decoding_error;
	}
	return wu_no_change;
}

static enum wu_error skyroads_dec(struct image_file *infile,
const struct wu_conf *conf) {
	enum wu_error st = skyroads_parse(infile->dec_state, infile->sub_img,
		infile->map);
	if (st == wu_ok && wuimg_exceeds_limit(infile->sub_img, conf)) {
		st = wu_exceeds_size_limit;
	}
	return st;
}

const struct image_fn skyroads_fn = {
	.mmap = true,
	.alloc_single = true,
	.state_size = sizeof(struct mparser),
	.dec = skyroads_dec,
	.callback = skyroads_callback,
};

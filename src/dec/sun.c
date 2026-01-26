// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2021 kaleido
#include "wudefs.h"
#include "lib/sun.h"

static struct wu_st event_sun(struct image_file *infile,
const struct wu_conf *wuconf, struct wu_state *state, enum image_event ev) {
	(void)wuconf; (void)state;
	struct sun_desc *desc = infile->dec_state;
	switch (ev) {
	case ev_metadata:
		tree_add_leaf_utf8(&infile->metadata, "Compressed",
			sun_type_str(desc->type));
		return WU_OK;
	case ev_subcycle:
		return sun_decode(desc, infile->sub_img);
	default: break;
	}
	return WU_NO_CHANGE;
}

static struct wu_st init_sun(struct image_file *infile,
const struct wu_conf *wuconf) {
	(void)wuconf;
	return sun_parse_header(infile->dec_state, infile->sub_img, infile->ifp);
}

const struct image_fn sun_fn = {
	.state_size = sizeof(struct sun_desc),
	.alloc_single = true,
	.alloc_on_subcycle = true,
	.init = init_sun,
	.event = event_sun,
};

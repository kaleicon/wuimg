// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2020 kaleido
#include "wudefs.h"
#include "lib/sgi.h"

static struct wu_st event_sgi(struct image_file *infile,
const struct wu_conf *wuconf, struct wu_state *state, enum image_event ev) {
	(void)wuconf; (void)state;
	const struct sgi_desc *desc = infile->dec_state;
	switch (ev) {
	case ev_metadata:
		tree_add_leaf_limit(&infile->metadata, "Name",
			WUPTR_ARRAY(desc->name), NULL);
		tree_add_leaf_utf8(&infile->metadata, "Compression",
			sgi_compression_str(desc->compression));
		return WU_OK;
	case ev_subcycle:
		return sgi_decode(desc, infile->sub_img);
	default: break;
	}
	return WU_NO_CHANGE;
}

static struct wu_st init_sgi(struct image_file *infile,
const struct wu_conf *wuconf) {
	(void)wuconf;
	return sgi_parse_header(infile->dec_state, infile->sub_img, infile->ifp);
}

const struct image_fn sgi_fn = {
	.state_size = sizeof(struct sgi_desc),
	.alloc_single = true,
	.alloc_on_subcycle = true,
	.init = init_sgi,
	.event = event_sgi,
};

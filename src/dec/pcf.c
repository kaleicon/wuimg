// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2024 kaleido
#include "lib/pcf.h"
#include "wudefs.h"

static void end_pcf(struct image_file *infile) {
	pcf_cleanup(infile->dec_state);
}

static struct wu_st event_pcf(struct image_file *infile,
struct wu_state *state, const enum image_event ev) {
	struct pcf_desc *desc = infile->dec_state;
	const uint32_t i = (uint32_t)state->idx;
	struct wuimg *img = infile->sub_img + i;
	switch (ev) {
	case ev_metadata:
		return pcf_set_glyph(desc, img, i);
	case ev_subcycle:
		return pcf_load_glyph(desc, img, i);
	default: break;
	}
	return WU_NO_CHANGE;
}

static void read_pcf_metadata(struct image_file *infile, struct pcf_desc *desc) {
	struct wutree *meta = &infile->metadata;
	bool all_ok = true;
	for (uint32_t i = 0; i < desc->prop.len; ++i) {
		struct pcf_property p;
		if (wu_isok(pcf_get_property(desc, &p, i))) {
			const char *name = (const char *)p.name.ptr;
			if (p.is_string) {
				tree_add_leaf_len(meta, name, p.val.s, NULL);
			} else {
				tree_bud_leaf_u(meta, name, p.val.i);
			}
		} else {
			all_ok = false;
		}
	}
	if (!all_ok && desc->prop.len) {
		image_file_strerror_append(infile, "Some font properties"
			" couldn't be read.");
	}
}

static struct wu_st init_pcf(struct image_file *infile) {
	struct pcf_desc *desc = infile->dec_state;
	struct wu_st st = pcf_parse(desc, infile->ifp);
	if (wu_isok(st)) {
		if (alloc_sub_images(infile, desc->glyphs)) {
			read_pcf_metadata(infile, desc);
			return WU_OK;
		}
		return WUERR_HERE(wu_alloc_error);
	}
	return st;
}

const struct image_fn pcf_fn = {
	.alloc_on_subcycle = true,
	.state_size = sizeof(struct pcf_desc),
	.init = init_pcf,
	.event = event_pcf,
	.end = end_pcf,
};

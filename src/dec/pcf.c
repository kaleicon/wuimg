// SPDX-License-Identifier: 0BSD
#include "lib/pcf.h"
#include "wudefs.h"

static void pcf_end(struct image_file *infile) {
	pcf_cleanup(infile->dec_state);
}

static enum wu_error pcf_callback(struct image_file *infile,
const struct wu_conf *wuconf, struct wu_state *state,
const enum image_event ev) {
	if (ev == ev_subcycle) {
		struct pcf_desc *desc = infile->dec_state;
		const uint32_t i = (uint32_t)state->idx;
		struct wuimg *img = infile->sub_img + i;
		const enum wu_error st = pcf_set_glyph(desc, img, i);
		if (st == wu_ok) {
			if (wuimg_exceeds_limit(img, wuconf)) {
				return wu_exceeds_size_limit;
			}
			return pcf_load_glyph(desc, img)
				? wu_ok : wu_decoding_error;
		}
		return st;
	}
	return wu_no_change;
}

static void read_metadata(struct image_file *infile, struct pcf_desc *desc) {
	struct wu_tree *meta = &infile->metadata;
	bool all_ok = true;
	for (uint32_t i = 0; i < desc->prop.len; ++i) {
		struct pcf_property p;
		if (pcf_get_property(desc, i, &p) == wu_ok) {
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

static enum wu_error pcf_dec(struct image_file *infile,
const struct wu_conf *wuconf) {
	(void)wuconf;
	struct pcf_desc *desc = infile->dec_state;
	enum wu_error st = pcf_open(desc, infile->ifp);
	if (st == wu_ok) {
		st = pcf_parse(desc);
		if (st == wu_ok) {
			if (alloc_sub_images(infile, desc->glyphs)) {
				read_metadata(infile, desc);
				return wu_ok;
			}
			return wu_alloc_error;
		}
	}
	return st;
}

const struct image_fn pcf_fn = {
	.state_size = sizeof(struct pcf_desc),
	.dec = pcf_dec,
	.callback = pcf_callback,
	.end = pcf_end,
};

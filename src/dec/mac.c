// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2020 kaleido
#include "lib/mac.h"
#include "wudefs.h"

static void read_macbin_metadata(const struct mac_binary_header *macbin,
struct wutree *tree) {
	if (!tree) {
		return;
	}

	struct wutree *file = tree_add_branch(tree, "File");
	if (file) {
		tree_add_leaf_len(file, "Name",
			wuptr_mem(macbin->name, macbin->name_len), NULL);
		tree_add_leaf_len(file, "Type",
			WUPTR_ARRAY(macbin->type), NULL);
		tree_add_leaf_len(file, "Creator",
			WUPTR_ARRAY(macbin->creator), NULL);

		tree_bud_leaf_u(file, "Attributes", macbin->attributes);
		tree_bud_leaf_u(file, "Protected", macbin->protection);
		tree_bud_leaf_time(file, "Created",
			mac_time_to_unix(macbin->time.created));
		tree_bud_leaf_time(file, "Last modified",
			mac_time_to_unix(macbin->time.modified));
	}

	struct wutree *window = tree_add_branch(tree, "Window");
	if (window) {
		tree_bud_leaf_u(window, "Y", macbin->window.y);
		tree_bud_leaf_u(window, "X", macbin->window.x);
		tree_bud_leaf_u(window, "ID", macbin->window.id);
	}
}

static struct wu_st event_mac(struct image_file *infile,
struct wu_state *state, const enum image_event ev) {
	if (ev == ev_subcycle) {
		struct wuimg *img = infile->sub_img + state->idx;
		return state->idx == 0
			? mac_decode(infile->dec_state, img)
			: mac_patterns_load(infile->dec_state, img);
	}
	return WU_NO_CHANGE;
}

static struct wu_st init_mac(struct image_file *infile) {
	struct mac_desc *desc = infile->dec_state;
	struct wu_st st = mac_open_file(desc, infile->map);
	if (!wu_isok(st)) {
		return st;
	}

	if (desc->has_macbin_header) {
		read_macbin_metadata(&desc->macbin,
			tree_add_branch(&infile->metadata, "MacBinary"));
	}
	tree_bud_leaf_u(&infile->metadata, "Version", desc->version);

	struct wuimg *img = alloc_sub_images(infile,
		desc->has_patterns ? 2 : 1);
	if (!img) {
		return WUERR_HERE(wu_alloc_error);
	}
	mac_get_sizes(img, desc->has_patterns ? img + 1 : NULL);
	return WU_OK;
}

const struct image_fn mac_fn = {
	.mmap = true,
	.state_size = sizeof(struct mac_desc),
	.alloc_on_subcycle = true,
	.init = init_mac,
	.event = event_mac,
};

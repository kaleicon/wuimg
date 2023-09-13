// SPDX-License-Identifier: 0BSD
#include "lib/mac.h"
#include "misc/common.h"
#include "wudefs.h"

static void read_macbin_metadata(const struct mac_binary_header *macbin,
struct wu_tree *tree) {
	if (!tree) {
		return;
	}

	struct wu_tree *file = tree_add_branch(tree, "File");
	if (file) {
		tree_add_leaf_len(file, "Name",
			wuptr_mem(macbin->name, macbin->name_len), NULL);
		tree_add_leaf_len(file, "Type", WUPTR_ARRAY(macbin->type), NULL);
		tree_add_leaf_len(file, "Creator", WUPTR_ARRAY(macbin->creator),
			NULL);

		tree_bud_leaf_u(file, "Attributes", macbin->attributes);
		tree_bud_leaf_u(file, "Protected", macbin->protection);
		tree_bud_leaf_time(file, "Created",
			mac_time_to_unix(macbin->time.created));
		tree_bud_leaf_time(file, "Last modified",
			mac_time_to_unix(macbin->time.modified));
	}

	struct wu_tree *window = tree_add_branch(tree, "Window");
	if (window) {
		tree_bud_leaf_u(window, "Y", macbin->window.y);
		tree_bud_leaf_u(window, "X", macbin->window.x);
		tree_bud_leaf_u(window, "ID", macbin->window.id);
	}
}

enum wu_error mac_dec(struct image_file *infile, const struct wu_conf *conf) {
	if (conf->max_img_size < 720) {
		return wu_exceeds_size_limit;
	}

	struct mac_desc desc;
	const enum wu_error st = mac_open_file(&desc, infile->ifp);
	if (st != wu_ok) {
		return st;
	}

	if (desc.has_macbin_header) {
		read_macbin_metadata(&desc.macbin,
			tree_add_branch(&infile->metadata, "MacBinary"));
	}
	tree_bud_leaf_u(&infile->metadata, "Version", desc.version);

	struct wuimg *img = alloc_sub_images(infile, desc.has_patterns ? 2 : 1);
	if (!img) {
		return wu_alloc_error;
	}

	mac_get_sizes(img, desc.has_patterns ? img + 1 : NULL);

	if (infile->nr == 2) {
		if (!mac_patterns_load(&desc, img + 1)) {
			image_file_strerror_append(infile,
				"Couldn't load pattern data");
			realloc_sub_images(infile, 1);
		}
	}
	return mac_decode(&desc, img) ? wu_ok : wu_decoding_error;
}

const struct image_fn mac_fn = {.dec = mac_dec};

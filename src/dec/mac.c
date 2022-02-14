#include <stdio.h>
#include <string.h>
#include <time.h>

#include "../wudefs.h"
#include "../common.h"
#include "../rast_utils.h"
#include "../lib/mac.h"

static void read_macbin_metadata(const struct mac_binary_header *macbin,
struct wu_tree *tree) {
	if (!tree) {
		return;
	}

	struct wu_tree *file_branch = tree_sprout_branch(tree, "File");
	if (file_branch) {
		tree_sprout_unsafe_leaf(file_branch, "Name", macbin->name,
			macbin->name_len);
		tree_sprout_unsafe_leaf(file_branch, "Type", macbin->type,
			sizeof(macbin->type));
		tree_sprout_unsafe_leaf(file_branch, "Creator",
			macbin->creator, sizeof(macbin->creator));

		const time_t macos_epoch_diff = 2082844800;
		const struct wu_tree_sap sap[] = {
			{"Attributes", wu_leaf_unsigned,
				{.u = macbin->attributes}},
			{"Protected", wu_leaf_unsigned,
				{.u = macbin->protection}},
			{"Created", wu_leaf_time,
				{.time = macbin->time.created - macos_epoch_diff}},
			{"Last modified", wu_leaf_time,
				{.time = macbin->time.modified - macos_epoch_diff}},
		};
		tree_bud_leaves(file_branch, sap, ARRAY_LEN(sap));
	}

	struct wu_tree *window_branch = tree_sprout_branch(tree, "Window");
	if (window_branch) {
		const struct wu_tree_sap sap[] = {
			{"y", wu_leaf_unsigned, {.u = macbin->window.y}},
			{"x", wu_leaf_unsigned, {.u = macbin->window.x}},
			{"id", wu_leaf_unsigned, {.u = macbin->window.id}},
		};
		tree_bud_leaves(window_branch, sap, ARRAY_LEN(sap));
	}
}

enum wu_error mac_dec(struct image_file *infile, const struct wu_conf *conf) {
	if (conf->max_img_size < 720) {
		return wu_exceeds_size_limit;
	}

	struct mac_desc desc;
	const enum lib_fail status = mac_open_file(&desc, infile->ifp);
	if (status != lib_ok) {
		rast_error(infile, status);
		return wu_open_error;
	}

	if (desc.has_macbin_header) {
		read_macbin_metadata(&desc.macbin,
			tree_sprout_branch(&infile->metadata, "MacBinary"));
	}
	tree_bud_leaf(&infile->metadata, "Version",
		(struct wu_leaf){.val.u = desc.version, .type = wu_leaf_unsigned});

	struct raw_img *img = alloc_sub_images(infile, desc.version ? 2 : 1);
	if (!img) {
		return wu_alloc_error;
	}

	// Read patterns first to keep access sequential
	if (infile->nr == 2) {
		const char *err = NULL;
		if (!rast_to_raw_img(&desc.patterns, img + 1)) {
			err = "Couldn't allocate memory for pattern";
		} else if (!mac_patterns_load(&desc, img[1].data)) {
			err = "Couldn't load pattern data";
		}

		if (err) {
			image_file_error_append(infile, err);
			realloc_sub_images(infile, 1);
		} else {
			img[1].id = strdup("patterns");
		}
	}

	img[0].data = mac_decode(&desc);
	if (img[0].data) {
		rast_to_raw(img, &desc.rast);
		return wu_ok;
	}
	return wu_decoding_error;
}

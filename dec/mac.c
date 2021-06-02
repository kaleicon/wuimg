#include <stdio.h>
#include <string.h>
#include <time.h>

#include "../wudefs.h"
#include "../common.h"
#include "lib/mac.h"

static void print_macbin_metadata(const struct mac_binary_header *macbin,
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
	const enum lib_fail status = mac_open_file(infile->ifp, &desc);
	if (status != lib_ok) {
		infile->err_msg = strdup(lib_fail_string(status));
		return wu_open_error;
	}

	if (desc.has_macbin_header) {
		print_macbin_metadata(&desc.macbin,
			tree_sprout_branch(&infile->metadata, "MacBinary"));
	}
	tree_bud_leaf(&infile->metadata, "Version", wu_leaf_unsigned,
		(union wu_leaf){.u = desc.version});

	struct raw_img *img = alloc_sub_images(infile, desc.version ? 2 : 1);
	if (!img) {
		return wu_alloc_error;
	}

	// Read the patterns first to keep access sequential
	if (infile->nr == 2) {
		img[1].data = mac_pattern_unpack(&desc);
		if (img[1].data) {
			img[1].w = 8;
			img[1].h = 8 * 38;
			img[1].channels = 1;
			img[1].bitdepth = 8;
			img[1].id = strdup("patterns");
		} else {
			infile->err_msg = strdup("Failed to allocate memory "
				"for pattern data.");
			realloc_sub_images(infile, 1);
		}
	}

	img[0].data = mac_decode(&desc);
	if (!img[0].data) {
		return wu_decoding_error;
	}
	img[0].w = 576;
	img[0].h = 720;
	img[0].channels = 1;
	img[0].bitdepth = 8;
	//munmap_stream(desc.file);
	return wu_ok;
}

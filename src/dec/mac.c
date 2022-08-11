#include <stdio.h>
#include <string.h>
#include <time.h>

#include "../wudefs.h"
#include "../lib/mac.h"

static void read_macbin_metadata(const struct mac_binary_header *macbin,
struct wu_tree *tree) {
	if (!tree) {
		return;
	}

	struct wu_tree *file_branch = tree_add_branch(tree, "File");
	if (file_branch) {
		tree_add_leaf_len(file_branch, "Name", macbin->name,
			macbin->name_len, NULL);
		tree_add_leaf_len(file_branch, "Type", macbin->type,
			sizeof(macbin->type), NULL);
		tree_add_leaf_len(file_branch, "Creator", macbin->creator,
			sizeof(macbin->creator), NULL);

		const struct wu_tree_sap sap[] = {
			{"Attributes", {wu_leaf_unsigned,
				{.u = macbin->attributes}}},
			{"Protected", {wu_leaf_unsigned,
				{.u = macbin->protection}}},
			{"Created", {wu_leaf_time,
				{.time = mac_time_to_unix(macbin->time.created)}}},
			{"Last modified", {wu_leaf_time,
				{.time = mac_time_to_unix(macbin->time.modified)}}},
		};
		tree_bud_leaves(file_branch, sap, ARRAY_LEN(sap));
	}

	struct wu_tree *window_branch = tree_add_branch(tree, "Window");
	if (window_branch) {
		const struct wu_tree_sap sap[] = {
			{"y", {wu_leaf_unsigned, {.u = macbin->window.y}}},
			{"x", {wu_leaf_unsigned, {.u = macbin->window.x}}},
			{"id", {wu_leaf_unsigned, {.u = macbin->window.id}}},
		};
		tree_bud_leaves(window_branch, sap, ARRAY_LEN(sap));
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
	tree_bud_leaf(&infile->metadata, "Version",
		(struct wu_leaf){.val.u = desc.version, .type = wu_leaf_unsigned});

	struct raw_img *img = alloc_sub_images(infile, desc.has_patterns ? 2 : 1);
	if (!img) {
		return wu_alloc_error;
	}

	mac_get_sizes(img, desc.has_patterns ? img + 1 : NULL);

	if (infile->nr == 2) {
		if (!mac_patterns_load(&desc, img + 1)) {
			image_file_error_append(infile,
				"Couldn't load pattern data");
			realloc_sub_images(infile, 1);
		}
	}
	return mac_decode(&desc, img) ? wu_ok : wu_decoding_error;
}

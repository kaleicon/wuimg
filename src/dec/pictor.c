#include <stdio.h>
#include <stdbool.h>
#include <string.h>

#include "../wudefs.h"
#include "../common.h"
#include "../lib/pictor.h"

static void read_metadata(struct wu_tree *tree,
const struct pictor_desc *desc) {
	const struct wu_tree_sap sap[] = {
		{"X", {wu_leaf_unsigned, {.u = desc->x}}},
		{"Y", {wu_leaf_unsigned, {.u = desc->y}}},
		{"Compressed blocks", {wu_leaf_unsigned, {.u = desc->blocks}}},
		{"Planes", {wu_leaf_unsigned, {.u = desc->planes}}},
		{"Depth", {wu_leaf_unsigned, {.u = desc->depth}}},
	};
	tree_bud_leaves(tree, sap, ARRAY_LEN(sap));

	const char *mode = pictor_video_mode(desc);
	if (mode) {
		tree_sprout_leaf(tree, "Video mode", mode);
	}

	const char *paltype = pictor_palette_str(desc->pal_type);
	if (paltype) {
		tree_sprout_leaf(tree, "Palette type", paltype);
	}
}

enum wu_error pictor_dec(struct image_file *infile,
const struct wu_conf *conf) {
	struct pictor_desc desc;
	enum wu_error st = pictor_open_file(&desc, infile->ifp);
	if (st == wu_ok) {
		struct raw_img *img = alloc_sub_images(infile, 1);
		if (img) {
			st = pictor_read_header(&desc, img);
			if (st == wu_ok) {
				read_metadata(&infile->metadata, &desc);
				if (!raw_img_exceeds_limit(img, conf)) {
					return pictor_decode(&desc, img)
						? wu_ok : wu_decoding_error;
				}
				return wu_exceeds_size_limit;
			}
			return st;
		}
		return wu_alloc_error;
	}
	return st;

}

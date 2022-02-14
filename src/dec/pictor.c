#include <stdio.h>
#include <stdbool.h>
#include <string.h>

#include "../wudefs.h"
#include "../common.h"
#include "../rast_utils.h"
#include "../lib/pictor.h"

static void read_metadata(struct wu_tree *tree,
const struct pictor_desc *desc) {
	const struct wu_tree_sap sap[] = {
		{"X", wu_leaf_unsigned, {.u = desc->x}},
		{"Y", wu_leaf_unsigned, {.u = desc->y}},
		{"Compressed blocks", wu_leaf_unsigned, {.u = desc->blocks}},
		{"Planes", wu_leaf_unsigned, {.u = desc->planes}},
		{"Depth", wu_leaf_unsigned, {.u = desc->depth}},
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

static enum wu_error dec_wrap(struct image_file *infile,
const struct wu_conf *conf, struct pictor_desc *desc) {
	const enum lib_fail status = pictor_read_header(desc);
	if (status != lib_ok) {
		rast_error(infile, status);
		return wu_invalid_header;
	}

	read_metadata(&infile->metadata, desc);

	if (rast_exceeds_size(&desc->r, conf)) {
		return wu_exceeds_size_limit;
	}

	struct raw_img *img = alloc_sub_images(infile, 1);
	if (!img || !rast_to_raw_img(&desc->r, img)) {
		return wu_alloc_error;
	}

	img->mirror = true;
	return pictor_decode(desc, img->data) ? wu_ok : wu_decoding_error;
}

enum wu_error pictor_dec(struct image_file *infile,
const struct wu_conf *conf) {
	struct pictor_desc desc;
	enum lib_fail status = pictor_open_file(&desc, infile->ifp);
	if (status == lib_ok) {
		const enum wu_error err = dec_wrap(infile, conf, &desc);
		pictor_cleanup(&desc);
		return err;
	}
	rast_error(infile, status);
	return wu_open_error;
}

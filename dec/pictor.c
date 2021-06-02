#include <stdio.h>
#include <stdbool.h>
#include <string.h>

#include "../wudefs.h"
#include "../common.h"

#include "lib/pictor.h"

static void print_metadata(struct wu_tree *tree,
const struct pictor_desc *desc) {
	const struct wu_tree_sap sap[] = {
		{"Planes", wu_leaf_unsigned, {.u = desc->planes}},
		{"Bitdepth", wu_leaf_unsigned, {.u = desc->bitdepth}},
	};
	tree_bud_leaves(tree, sap, ARRAY_LEN(sap));

	const char *mode = pictor_video_mode(desc);
	if (mode) {
		tree_sprout_leaf(tree, "Video mode", mode);
	}

	const char *paltype = NULL;
	switch (desc->palette.type) {
	case cga_palette: paltype = "CGA"; break;
	case pcjr_palette: paltype = "PCJr"; break;
	case ega_palette: paltype = "EGA"; break;
	case vga_palette:
	case vga_too_i_think: paltype = "VGA"; break;
	default: break;
	}

	if (paltype) {
		tree_sprout_leaf(tree, "Palette type", mode);
	}
}

enum wu_error pictor_dec(struct image_file *infile,
const struct wu_conf *conf) {
	struct pictor_desc desc;
	enum lib_fail status = pictor_open_file(infile->ifp, &desc);
	if (status != lib_ok) {
		infile->err_msg = strdup(lib_fail_string(status));
		return wu_open_error;
	}

	status = pictor_read_header(&desc);
	if (status != lib_ok) {
		pictor_cleanup(&desc);
		infile->err_msg = strdup(lib_fail_string(status));
		return wu_invalid_header;
	}

	print_metadata(&infile->metadata, &desc);

	if (umax(desc.w, desc.h) > conf->max_img_size) {
		pictor_cleanup(&desc);
		return wu_exceeds_size_limit;
	}

	struct raw_img *img = alloc_sub_images(infile, 1);
	if (!img) {
		pictor_cleanup(&desc);
		return wu_alloc_error;
	}

	img->palette = pictor_take_palette(&desc);
	img->data = pictor_decode(&desc);
	if (img->data) {
		img->w = desc.w;
		img->h = desc.h;
		img->channels = desc.planes;
		img->bitdepth = 8;
		img->mirror = true;
	}
	return img->data ? wu_ok : wu_decoding_error;
}

#include <stdio.h>
#include <stdbool.h>
#include <string.h>

#include "../wudefs.h"
#include "../common.h"
#include "../rast_utils.h"
#include "../lib/pictor.h"

static void print_metadata(struct wu_tree *tree,
const struct pictor_desc *desc) {
	const struct wu_tree_sap sap[] = {
		{"Compressed blocks", wu_leaf_unsigned, {.u = desc->blocks}},
		{"Planes", wu_leaf_unsigned, {.u = desc->r.ch}},
		{"Bitdepth", wu_leaf_unsigned, {.u = desc->r.bitdepth}},
	};
	tree_bud_leaves(tree, sap, ARRAY_LEN(sap));

	const char *mode = pictor_video_mode(desc);
	if (mode) {
		tree_sprout_leaf(tree, "Video mode", mode);
	}

	const char *paltype = NULL;
	switch (desc->pal_type) {
	case pictor_cga_palette: paltype = "CGA"; break;
	case pictor_pcjr_palette: paltype = "PCJr"; break;
	case pictor_ega_palette: paltype = "EGA"; break;
	case pictor_vga_palette:
	case pictor_vga_too_i_think: paltype = "VGA"; break;
	default: break;
	}

	if (paltype) {
		tree_sprout_leaf(tree, "Palette type", paltype);
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

	if (rast_exceeds_size(&desc.r, conf)) {
		pictor_cleanup(&desc);
		return wu_exceeds_size_limit;
	}

	struct raw_img *img = alloc_sub_images(infile, 1);
	if (!img) {
		pictor_cleanup(&desc);
		return wu_alloc_error;
	}

	img->data = pictor_decode(&desc);
	rast_to_raw(img, &desc.r);
	pictor_cleanup(&desc);
	if (img->data) {
		img->mirror = true;
		return wu_ok;
	}
	return wu_decoding_error;
}

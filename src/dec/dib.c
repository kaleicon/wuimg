#include "../wudefs.h"
#include "../common.h"
#include "../rast_utils.h"

#include "../lib/dib.h"

static void get_dib_metadata(struct wu_tree *tree, const struct dib_desc *desc) {
	tree_sprout_leaf(tree, "Header", dib_type_str(desc->type));
	tree_sprout_leaf(tree, "Compression", dib_compression_str(desc->compression));

	struct wu_leaf leaf = {.val.u = desc->depth, .type = wu_leaf_unsigned};
	tree_bud_leaf(tree, "Depth", leaf);
}

static enum wu_error dib_common(struct image_file *infile,
const struct wu_conf *wuconf, struct dib_desc *desc) {
	if (rast_exceeds_size(&desc->r, wuconf)) {
		dib_cleanup(desc);
		return wu_exceeds_size_limit;
	}

	get_dib_metadata(&infile->metadata, desc);

	struct raw_img *img = alloc_sub_images(infile, 1);
	if (!img) {
		dib_cleanup(desc);
		return wu_alloc_error;
	}

	img->mirror = (desc->order == dib_bottom_up);
	if (desc->compression == dib_no_compression) {
		if (desc->depth == 16) {// || desc.depth == 32) {
			img->disable_alpha = true;
		}
	}

	img->data = dib_decode(desc);
	rast_to_raw(img, &desc->r);
	dib_cleanup(desc);
	return img->data ? wu_ok : wu_decoding_error;
}

enum wu_error bmp_dec(struct image_file *infile, const struct wu_conf *wuconf) {
	struct dib_desc desc;
	enum lib_fail fail = bmp_open_file(&desc, infile->ifp);
	if (fail) {
		return wu_invalid_header;
	}

	fail = bmp_parse_header(&desc);
	if (fail) {
		dib_cleanup(&desc);
		return wu_invalid_header;
	}
	return dib_common(infile, wuconf, &desc);
}

enum wu_error dib_dec(struct image_file *infile, const struct wu_conf *wuconf) {
	struct dib_desc desc;
	if (dib_open_file(&desc, infile->ifp) != lib_ok) {
		dib_cleanup(&desc);
		return wu_invalid_header;
	}
	return dib_common(infile, wuconf, &desc);
}

enum wu_error ico_dec(struct image_file *infile, const struct wu_conf *wuconf) {
	struct ico_desc desc;
	enum lib_fail status = ico_open_file(&desc, infile->ifp);
	if (status != lib_ok) {
		return wu_invalid_header;
	}

	status = ico_parse_header(&desc);
	if (status != lib_ok) {
		ico_cleanup(&desc);
		return wu_invalid_header;
	}

	tree_sprout_leaf(&infile->metadata, "Type", ico_type_str(desc.type));

	struct raw_img *img = alloc_sub_images(infile, desc.count);
	if (!img) {
		ico_cleanup(&desc);
		return wu_alloc_error;
	}

	size_t o = 0;
	for (uint16_t i = 0; i < desc.count; ++i) {
		status = ico_set_image(&desc, i);
		if (status != lib_ok) {
			continue;
		}

		if (rast_exceeds_size(&desc.dib.r, wuconf)) {
			continue;
		}

		img[o].mirror = (desc.dib.order == dib_bottom_up);
		img[o].data = ico_decode(&desc);
		rast_to_raw(img + o, &desc.dib.r);
		if (!img[o].data) {
			continue;
		}
		++o;
	}
	ico_cleanup(&desc);
	return image_file_total_decoded(infile, o);
}

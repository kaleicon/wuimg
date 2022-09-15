#include "lib/dib.h"

static void get_dib_metadata(struct wu_tree *tree, const struct dib_desc *desc) {
	tree_add_leaf_utf8(tree, "Header", dib_type_str(desc));
	tree_add_leaf_utf8(tree, "Compression",
		dib_compression_str(desc->compression));

	struct wu_leaf leaf = {.val.u = desc->depth, .type = wu_leaf_unsigned};
	tree_bud_leaf(tree, "Depth", leaf);
}

static enum wu_error dib_common(struct image_file *infile,
const struct wu_conf *wuconf, struct raw_img *img, struct dib_desc *desc) {
	if (raw_img_exceeds_limit(img, wuconf)) {
		return wu_exceeds_size_limit;
	}

	get_dib_metadata(&infile->metadata, desc);

	if (dib_decode(desc, img)) {
		struct wustr name;
		if (dib_get_linked_profile_name(desc, &name)) {
			tree_add_leaf_len(&infile->metadata, "Linked profile",
				name.str, name.len, NULL);
			wustr_free(&name);
		}
		return wu_ok;
	}
	return wu_decoding_error;
}

enum wu_error bmp_dec(struct image_file *infile, const struct wu_conf *wuconf) {
	struct dib_desc desc;
	enum wu_error err = bmp_open_file(&desc, infile->ifp);
	if (err == wu_ok) {
		struct raw_img *img = alloc_sub_images(infile, 1);
		if (img) {
			err = bmp_parse_header(&desc, img);
			if (err == wu_ok) {
				return dib_common(infile, wuconf, img, &desc);
			}
			return err;
		}
		return wu_alloc_error;
	}
	return err;
}

enum wu_error dib_dec(struct image_file *infile, const struct wu_conf *wuconf) {
	struct raw_img *img = alloc_sub_images(infile, 1);
	if (img) {
		struct dib_desc desc;
		const enum wu_error err = dib_open_file(&desc, img, infile->ifp);
		if (err == wu_ok) {
			return dib_common(infile, wuconf, img, &desc);
		}
		return err;
	}
	return wu_alloc_error;
}

static enum wu_error wrap_ico(struct image_file *infile,
const struct wu_conf *wuconf, struct ico_desc *desc) {
	const enum wu_error status = ico_parse_header(desc);
	if (status != wu_ok) {
		return status;
	}

	tree_add_leaf_utf8(&infile->metadata, "Type", ico_type_str(desc->type));

	if (!alloc_sub_images(infile, desc->count)) {
		return wu_alloc_error;
	}

	size_t o = 0;
	for (uint16_t i = 0; i < desc->count; ++i) {
		struct raw_img *img = infile->sub_img + o;
		if (ico_set_image(desc, img, i) == wu_ok
		&& !raw_img_exceeds_limit(img, wuconf)
		&& ico_decode(desc, img)) {
			++o;
		} else {
			raw_img_clear(img);
		}
	}
	return image_file_total_decoded(infile, o);
}

enum wu_error ico_dec(struct image_file *infile, const struct wu_conf *wuconf) {
	struct ico_desc desc;
	enum wu_error err = ico_open_file(&desc, infile->ifp);
	if (err == wu_ok) {
		err = wrap_ico(infile, wuconf, &desc);
		ico_cleanup(&desc);
	}
	return err;
}

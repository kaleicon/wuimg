// SPDX-License-Identifier: 0BSD
#include "lib/dib.h"
#include "wudefs.h"

static enum wu_error dib_common(struct image_file *infile,
const struct wu_conf *wuconf, struct dib_desc *desc) {
	struct wuimg *img = alloc_sub_images(infile, 1);
	if (!img) {
		return wu_alloc_error;
	}

	const enum wu_error err = dib_parse_header(desc, img);
	if (err != wu_ok) {
		return err;
	}

	if (wuimg_exceeds_limit(img, wuconf)) {
		return wu_exceeds_size_limit;
	}

	if (dib_decode(desc, img)) {
		struct wu_tree *tree = &infile->metadata;
		tree_add_leaf_utf8(tree, "Header", dib_type_str(desc));
		tree_add_leaf_utf8(tree, "Compression",
			dib_compression_str(desc->compression));
		tree_bud_leaf_u(tree, "Depth", desc->depth);

		struct wustr name;
		if (dib_get_linked_profile_name(desc, &name)) {
			tree_add_leaf_len(tree, "Linked profile",
				wuptr_wustr(name), NULL);
			wustr_free(&name);
		}
		return wu_ok;
	}
	return wu_decoding_error;
}

static enum wu_error decode_dib(struct image_file *infile,
const struct wu_conf *wuconf, const bool is_bmp) {
	struct dib_desc desc;
	const enum wu_error err = dib_open_file(&desc, infile->ifp, is_bmp,
		trit_what);
	if (err == wu_ok) {
		return dib_common(infile, wuconf, &desc);
	}
	return err;
}

static enum wu_error bmp_dec(struct image_file *infile,
const struct wu_conf *wuconf) {
	return decode_dib(infile, wuconf, true);
}

static enum wu_error dib_dec(struct image_file *infile,
const struct wu_conf *wuconf) {
	return decode_dib(infile, wuconf, false);
}

const struct image_fn bmp_fn = {.dec = bmp_dec};
const struct image_fn dib_fn = {.dec = dib_dec};


static void ico_end(struct image_file *infile) {
	ico_cleanup(infile->dec_state);
	free(infile->dec_state);
}

static enum wu_error wrap_ico(struct wuimg *img, const struct wu_conf *wuconf,
struct ico_desc *desc, const uint16_t idx) {
	const enum wu_error st = ico_set_image(desc, img, idx);
	if (st == wu_ok) {
		if (!wuimg_exceeds_limit(img, wuconf)) {
			return ico_decode(desc, img) ? wu_ok : wu_decoding_error;
		}
		return wu_exceeds_size_limit;
	}
	return st;
}

static enum wu_error ico_callback(struct image_file *infile,
const struct wu_conf *wuconf, struct wu_state *state, const enum image_event ev) {
	(void)ev;
	const uint16_t idx = (uint16_t)state->idx;
	struct wuimg *img = infile->sub_img + idx;
	return wrap_ico(img, wuconf, infile->dec_state, idx);
}

static enum wu_error ico_dec(struct image_file *infile,
const struct wu_conf *wuconf) {
	(void)wuconf;
	struct ico_desc *desc = malloc(sizeof(*desc));
	if (desc) {
		infile->dec_state = desc;
		enum wu_error err = ico_open_file(desc, infile->ifp);
		if (err == wu_ok) {
			err = ico_parse_header(desc);
			if (err == wu_ok) {
				tree_add_leaf_utf8(&infile->metadata, "Type",
					ico_type_str(desc->type));
				return alloc_sub_images(infile, desc->count)
					? wu_ok : wu_alloc_error;
			}
		}
		return err;
	}
	return wu_alloc_error;
}

const struct image_fn ico_fn = {
	.dec = ico_dec,
	.callback = ico_callback,
	.end = ico_end,
};


#include "dec_enable.def"
#ifdef WU_ENABLE_BMZ
static enum wu_error bmz_dec(struct image_file *infile,
const struct wu_conf *wuconf) {
	struct bmz_desc desc;
	enum wu_error st = bmz_open(&desc, mp_map(infile->map));
	if (st == wu_ok) {
		st = dib_common(infile, wuconf, &desc.bmp);
		bmz_cleanup(&desc);
	}
	return st;
}

const struct image_fn bmz_fn = {.mmap = true, .dec = bmz_dec};
#endif /* WU_ENABLE_BMZ */

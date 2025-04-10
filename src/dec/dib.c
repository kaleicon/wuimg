// SPDX-License-Identifier: 0BSD
#include "lib/dib.h"
#include "wudefs.h"

static struct wu_st dib_common(struct image_file *infile,
const struct wu_conf *wuconf, struct dib_desc *desc) {
	struct wuimg *img = infile->sub_img;
	const struct wu_st st = dib_parse_header(desc, img);
	if (!wu_isok(st)) {
		return st;
	}

	if (wuimg_exceeds_limit(img, wuconf)) {
		return wuerr(wu_exceeds_size_limit, NULL);
	}

	if (dib_decode(desc, img)) {
		struct wutree *tree = &infile->metadata;
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
		return wuok();
	}
	return wuerr(wu_decoding_error, NULL);
}

static struct wu_st decode_dib(struct image_file *infile,
const struct wu_conf *wuconf, const bool is_bmp) {
	struct dib_desc desc;
	const struct wu_st st = dib_open_file(&desc, infile->ifp, is_bmp,
		trit_what);
	if (wu_isok(st)) {
		return dib_common(infile, wuconf, &desc);
	}
	return st;
}

static struct wu_st init_bmp(struct image_file *infile,
const struct wu_conf *wuconf) {
	return decode_dib(infile, wuconf, true);
}

static struct wu_st init_dib(struct image_file *infile,
const struct wu_conf *wuconf) {
	return decode_dib(infile, wuconf, false);
}

const struct image_fn bmp_fn = {.alloc_single = true, .init = init_bmp};
const struct image_fn dib_fn = {.alloc_single = true, .init = init_dib};


static void ico_end(struct image_file *infile) {
	ico_cleanup(infile->dec_state);
}

static enum wu_error wrap_ico(struct wuimg *img, const struct wu_conf *wuconf,
struct ico_desc *desc, const uint16_t idx) {
	const struct wu_st st = ico_set_image(desc, img, idx);
	if (wu_isok(st)) {
		if (!wuimg_exceeds_limit(img, wuconf)) {
			return ico_decode(desc, img) ? wu_ok : wu_decoding_error;
		}
		return wu_exceeds_size_limit;
	}
	return st.st;
}

static enum wu_error ico_callback(struct image_file *infile,
const struct wu_conf *wuconf, struct wu_state *state, const enum image_event ev) {
	(void)ev;
	const uint16_t idx = (uint16_t)state->idx;
	struct wuimg *img = infile->sub_img + idx;
	return (ev == ev_subcycle)
		? wrap_ico(img, wuconf, infile->dec_state, idx)
		: wu_no_change;
}

static struct wu_st init_ico(struct image_file *infile,
const struct wu_conf *wuconf) {
	(void)wuconf;
	struct ico_desc *desc = infile->dec_state;
	struct wu_st st = ico_open_file(desc, infile->ifp);
	if (wu_isok(st)) {
		st = ico_parse_header(desc);
		if (wu_isok(st)) {
			tree_add_leaf_utf8(&infile->metadata, "Type",
				ico_type_str(desc->type));
			return alloc_sub_images(infile, desc->count)
				? wuok() : WUERR_HERE(wu_alloc_error);
		}
	}
	return st;
}

const struct image_fn ico_fn = {
	.state_size = sizeof(struct ico_desc),
	.init = init_ico,
	.callback = ico_callback,
	.end = ico_end,
};


#include "dec_enable.def"
#ifdef WU_ENABLE_BMZ
static struct wu_st init_bmz(struct image_file *infile,
const struct wu_conf *wuconf) {
	struct bmz_desc desc;
	struct wu_st st = bmz_open(&desc, mp_wuptr(infile->map));
	if (wu_isok(st)) {
		st = dib_common(infile, wuconf, &desc.bmp);
		bmz_cleanup(&desc);
	}
	return st;
}

const struct image_fn bmz_fn = {
	.mmap = true,
	.alloc_single = true,
	.init = init_bmz,
};
#endif /* WU_ENABLE_BMZ */

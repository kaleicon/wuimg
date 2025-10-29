// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2021 kaleido
#include "lib/dib.h"
#include "wudefs.h"

static struct wu_st common_dib(struct image_file *infile,
const struct wu_conf *conf, struct dib_desc *desc) {
	struct wuimg *img = infile->sub_img;
	struct wu_st st = dib_parse_header(desc, img);
	if (!wu_isok(st)) {
		return st;
	}

	enum wu_error e = wuimg_alloc_limit(img, conf);
	if (e != wu_ok) {
		return WUERR_HERE(e);
	}

	st = dib_decode(desc, img);
	if (wu_isok(st)) {
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
	}
	return st;
}

static struct wu_st decode_dib(struct image_file *infile,
const struct wu_conf *wuconf, const bool is_bmp) {
	struct dib_desc desc;
	const struct wu_st st = dib_open_file(&desc, infile->ifp, is_bmp,
		trit_what);
	if (wu_isok(st)) {
		return common_dib(infile, wuconf, &desc);
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

const struct image_fn bmp_fn = {
	.alloc_single = true,
	.init = init_bmp
};
const struct image_fn dib_fn = {
	.alloc_single = true,
	.init = init_dib
};


static void end_ico(struct image_file *infile) {
	ico_cleanup(infile->dec_state);
}

static struct wu_st event_ico(struct image_file *infile,
const struct wu_conf *wuconf, struct wu_state *state, const enum image_event ev) {
	(void)wuconf;
	const uint16_t idx = (uint16_t)state->idx;
	struct wuimg *img = infile->sub_img + idx;
	switch (ev) {
	case ev_subcycle: return ico_decode(infile->dec_state, img);
	case ev_metadata: return ico_set_image(infile->dec_state, img, idx);
	default: break;
	}
	return WU_NO_CHANGE;
}

static struct wu_st init_ico(struct image_file *infile,
const struct wu_conf *wuconf) {
	(void)wuconf;
	struct ico_desc *desc = infile->dec_state;
	struct wu_st st = ico_parse(desc, infile->ifp);
	if (wu_isok(st)) {
		tree_add_leaf_utf8(&infile->metadata, "Type",
			ico_type_str(desc->type));
		infile->nr = desc->count;
	}
	return st;
}

const struct image_fn ico_fn = {
	.state_size = sizeof(struct ico_desc),
	.alloc_on_subcycle = true,
	.init = init_ico,
	.event = event_ico,
	.end = end_ico,
};


#include "dec_enable.def"
#ifdef WU_ENABLE_BMZ
static struct wu_st init_bmz(struct image_file *infile,
const struct wu_conf *wuconf) {
	struct bmz_desc desc;
	struct wu_st st = bmz_open(&desc, mp_wuptr(infile->map));
	if (wu_isok(st)) {
		st = common_dib(infile, wuconf, &desc.bmp);
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

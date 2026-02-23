// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2021 kaleido
#include <string.h>

#include "lib/dib.h"
#include "wudefs.h"

#include "dec_enable.def"
#include "dec.h"
#include "dec_fn.h"

static struct wu_st common_dib(struct image_file *infile,
struct dib_desc *desc) {
	struct wuimg *img = infile->sub_img;
	struct wu_st st = dib_parse_header(desc, img);
	if (!wu_isok(st)) {
		return st;
	}

	enum wu_error e = wuimg_alloc_limit(img, infile->conf);
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

static struct wu_st decode_dib(struct image_file *infile, const bool is_bmp) {
	struct dib_desc desc;
	const struct wu_st st = dib_open_file(&desc, infile->ifp, is_bmp,
		trit_what);
	if (wu_isok(st)) {
		return common_dib(infile, &desc);
	}
	return st;
}

static struct wu_st init_bmp(struct image_file *infile) {
	return decode_dib(infile, true);
}

static struct wu_st init_dib(struct image_file *infile) {
	return decode_dib(infile, false);
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
struct wu_state *state, const enum image_event ev) {
	const uint16_t idx = (uint16_t)state->idx;
	struct wuimg *img = infile->sub_img + idx;
	struct wu_st st = WU_NO_CHANGE;
	struct ico_desc *desc = infile->dec_state;
	switch (ev) {
	case ev_subcycle:
		if (desc->is_png) {
#ifdef WU_ENABLE_PNG
			struct wudec_image ctx = {.conf = *infile->conf};
			wudec_src_file(&ctx, infile->ifp, NULL, true, false);
			wudec_src_dec_fn(&ctx, &png_fn);
			st = wudec_decode_embedded(infile, img, &ctx);
			wudec_free(&ctx);
#endif // WU_ENABLE_PNG
		} else {
			enum wu_error e = wuimg_alloc_limit(img, infile->conf);
			st = (e == wu_ok)
				? ico_decode(desc, img)
				: WUERR_HERE(e);
		}
		break;
	case ev_metadata:
		st = ico_set_image(desc, img, idx);
		struct wutree *meta = wuimg_get_metadata(img);
		if (meta) {
			tree_add_leaf_utf8(meta, "Storage",
				desc->is_png ? "PNG" : "DIB");
		}
		if (desc->is_png) {
#ifdef WU_ENABLE_PNG
			st = WU_NO_CHANGE;
#else
			st = wuerr(wu_unsupported_feature, "sub-image is a PNG"
				" file but PNG support was not compiled-in");
#endif // WU_ENABLE_PNG
		}
		break;
	default: break;
	}
	return st;
}

static struct wu_st init_ico(struct image_file *infile) {
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
	.alloc_on_subcycle = false,
	.init = init_ico,
	.event = event_ico,
	.end = end_ico,
};


#include "dec_enable.def"
#ifdef WU_ENABLE_BMZ
static struct wu_st init_bmz(struct image_file *infile) {
	struct bmz_desc desc;
	struct wu_st st = bmz_open(&desc, mp_wuptr(infile->map));
	if (wu_isok(st)) {
		st = common_dib(infile, &desc.bmp);
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

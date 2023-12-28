// SPDX-License-Identifier: 0BSD
#include "rast_utils.h"
#include "lib/ilbm.h"

#define GET_CHAR(b, sh) (char)((b >> sh) & 0xff)
#define FOURCC_TO_STR(n) GET_CHAR(n, 24), GET_CHAR(n, 16), GET_CHAR(n, 8), GET_CHAR(n, 0), 0

static enum wu_error text_chunk_metadata(const uint32_t id,
const struct wuptr data, void *ptr) {
	const char name[] = {FOURCC_TO_STR(id)};
	tree_add_leaf_len(ptr, name, data, NULL);
	return wu_ok;
}
static enum wu_error chunk_metadata(const uint32_t id,
const struct wuptr data, void *ptr) {
	const char name[] = {FOURCC_TO_STR(id)};
	tree_bud_leaf_u(ptr, name, data.len);
	return wu_ok;
}

static void add_metadata(const struct ilbm_desc *desc, struct wu_tree *meta) {
	tree_bud_leaf_u(meta, "Depth", desc->planes);
	tree_bud_leaf_u(meta, "Colors", desc->colors);
	tree_bud_leaf_bool(meta, "Compression", desc->compression);
	if (desc->extra_half_brite) {
		tree_bud_leaf_bool(meta, "Extra Half Brite",
			desc->extra_half_brite);
	}
	if (desc->ham) {
		tree_bud_leaf_bool(meta, "HAM", desc->ham);
	}
	if (desc->crng.active_nr) {
		tree_bud_leaf_u(meta, "Active color ranges",
			desc->crng.active_nr);
	}
}

static void ilbm_end(struct image_file *infile) {
	ilbm_cleanup(infile->dec_state);
	free(infile->dec_state);
}

static enum wu_error ilbm_callback(struct image_file *infile,
const struct wu_conf *wuconf, struct wu_state *state,
const enum image_event ev) {
	(void)wuconf;
	struct ilbm_desc *desc = infile->dec_state;
	struct wuimg *img = infile->sub_img;
	switch (ev) {
	case ev_subcycle:
		if (!img[state->idx].data) {
			const bool ok = state->idx
				? ilbm_decode_tiny(desc, img, img + 1)
				: ilbm_decode_main(desc, img);
			return ok ? wu_ok : wu_decoding_error;
		}
		break;
	case ev_time:
		ilbm_palette_cycle(desc, img->u.palette, state->time);
		return wu_ok;
	default: break;
	}
	return wu_no_change;
}

static enum wu_error ilbm_dec(struct image_file *infile,
const struct wu_conf *wuconf) {
	(void)wuconf;
	struct ilbm_desc *desc = malloc(sizeof(*desc));
	if (!desc) {
		return wu_alloc_error;
	}
	infile->dec_state = desc;

	enum wu_error st = ilbm_open(desc, mp_map(infile->map));
	if (st != wu_ok) {
		return st;
	}
	struct wuimg *img = alloc_sub_images(infile, 1);
	if (!img) {
		return wu_alloc_error;
	}

	const char type[] = {FOURCC_TO_STR(desc->format)};
	struct wu_tree *type_meta = tree_add_branch(&infile->metadata, type);
	if (type_meta) {
		ilbm_set_callbacks(desc, chunk_metadata, text_chunk_metadata,
			type_meta);
	}

	st = ilbm_parse_header(desc, img);
	if (st != wu_ok) {
		return st;
	} else if (desc->crng.too_many) {
		image_file_strerror_append(infile,
			"Excess color cycles ignored");
	}
	ilbm_parse_footer(desc);

	if (desc->tiny.present) {
		realloc_sub_images(infile, 2);
	}
	add_metadata(desc, &infile->metadata);
	return wu_ok;
}

const struct image_fn ilbm_fn = {
	.mmap = true,
	.dec = ilbm_dec,
	.callback = ilbm_callback,
	.end = ilbm_end,
};

// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2023 kaleido
#include "lib/ilbm.h"
#include "wudefs.h"

#define FOURCC_STR_LEN 17

static void end_ilbm(struct image_file *infile) {
	ilbm_cleanup(infile->dec_state);
}

static struct wu_st event_ilbm(struct image_file *infile,
const struct wu_conf *conf, struct wu_state *state,
const enum image_event ev) {
	struct ilbm_desc *desc = infile->dec_state;
	int idx = state->idx;
	struct wuimg *img = infile->sub_img + idx;
	switch (ev) {
	case ev_subcycle:
		;enum wu_error err = wuimg_alloc_limit(img, conf);
		if (err == wu_ok) {
			return ilbm_decode(desc, img, idx != 0);
		}
		return WUERR_HERE(err);
	case ev_time:
		palette_cycle_render(img->u.palette, desc->cycle, state->time);
		return WU_OK;
	default: break;
	}
	return WU_NO_CHANGE;
}

static char tohex(uint8_t c) {
	c &= 0xf;
	if (c < 0xa) {
		return (char)(c + '0');
	}
	return (char)(c - 0xa + 'a');
}

static void fourcc_to_str(char str[static FOURCC_STR_LEN], uint32_t id) {
	size_t pos = 0;
	for (size_t i = 0; i < 4; ++i) {
		const uint8_t c = id >> (8*(3 - i)) & 0xff;
		if (c < ' ' || c > 0x7f) {
			str[pos] = '\\';
			str[pos+1] = 'x';
			str[pos+2] = tohex(c >> 4);
			str[pos+3] = tohex(c);
			pos += 4;
		} else {
			str[pos] = (char)c;
			++pos;
		}
	}
	str[pos] = 0;
}

static void chunk_metadata(void *ptr, const struct iff_chunk chunk,
const struct wuptr data) {
	char name[FOURCC_STR_LEN];
	fourcc_to_str(name, chunk.id);
	if (iff_is_text(chunk.id)) {
		tree_add_leaf_len(ptr, name, data, NULL);
	} else {
		tree_bud_leaf_u(ptr, name, data.len);
	}
}

static void add_metadata(const struct ilbm_desc *desc, struct wutree *meta) {
	tree_add_leaf_utf8(meta, "Compression",
		ilbm_compression_str(desc->compression));
	tree_bud_leaf_u(meta, "Depth", desc->planes);
	tree_bud_leaf_u(meta, "Colors", desc->colors);
	if (desc->extra_half_brite) {
		tree_bud_leaf_bool(meta, "Extra Half Brite",
			desc->extra_half_brite);
	}
	if (desc->ham) {
		tree_bud_leaf_bool(meta, "HAM", desc->ham);
	}
	if (desc->cycle) {
		tree_bud_leaf_u(meta, "Active color ranges",
			desc->cycle->active_nr);
	}
}

static struct wu_st init_ilbm(struct image_file *infile,
const struct wu_conf *_c) {
	(void)_c;
	struct ilbm_desc *desc = infile->dec_state;
	struct wu_st st = ilbm_open(desc, infile->map);
	if (!wu_isok(st)) {
		return st;
	}
	char type[FOURCC_STR_LEN];
	fourcc_to_str(type, desc->format);
	struct wutree *type_meta = tree_add_branch(&infile->metadata, type);
	if (type_meta) {
		ilbm_set_callbacks(desc, chunk_metadata, type_meta);
	}

	struct wuimg *img = infile->sub_img;
	st = ilbm_parse_header(desc, img);
	if (!wu_isok(st)) {
		return st;
	} else if (desc->cycle && desc->cycle->too_many) {
		image_file_strerror_append(infile,
			"Excess color cycles ignored");
	}
	ilbm_parse_footer(desc);

	if (desc->tiny.present) {
		img = realloc_sub_images(infile, 2);
		if (!img) {
			return WUERR_HERE(wu_alloc_error);
		}
		ilbm_setup_tiny(desc, img, img + 1);
	}
	add_metadata(desc, &infile->metadata);
	return WU_OK;
}

const struct image_fn ilbm_fn = {
	.mmap = true,
	.alloc_single = true,
	.state_size = sizeof(struct ilbm_desc),
	.init = init_ilbm,
	.event = event_ilbm,
	.end = end_ilbm,
};

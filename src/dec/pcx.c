// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2021 kaleido
#include <ctype.h>

#include "lib/pcx.h"
#include "wudefs.h"

static size_t is_readable_garbage(const unsigned char *data, const size_t len) {
	size_t i = 0;
	while (i < len) {
		if (!data[i]) {
			break;
		} else if (!isprint(data[i])) {
			return 0;
		}
		++i;
	}
	return i;
}

static void add_metadata(const struct pcx_desc *desc, struct wuimg *img) {
	struct wutree *metadata = wuimg_get_metadata(img);
	if (!metadata) {
		return;
	}

	tree_add_leaf_utf8(metadata, "Format version",
		pcx_version_string(desc->version));

	tree_bud_leaf_u(metadata, "Planes", img->channels);
	tree_bud_leaf_u(metadata, "Bitdepth", img->bitdepth);
	tree_bud_leaf_u(metadata, "Palette mode", desc->palette_type);

	if (desc->entries <= 4) {
		const void *garbage = desc->file_pal + 12;
		const size_t len = is_readable_garbage(garbage,
			sizeof(desc->file_pal) - 12);
		if (len > 3) {
			tree_add_leaf_len(metadata, "Garbage",
				wuptr_mem(garbage, len), NULL);
		}
	}
}

static struct wu_st common_pcx(struct pcx_desc *desc, struct wuimg *img,
const struct wu_conf *wuconf) {
	const struct wu_st st = pcx_read_header(desc, img);
	if (wu_isok(st)) {
		add_metadata(desc, img);
		if (wuimg_exceeds_limit(img, wuconf)) {
			return wuerr(wu_exceeds_size_limit, NULL);
		}
		return pcx_decode(desc, img);
	}
	return st;
}

static enum wu_error pcx_dec(struct image_file *infile,
const struct wu_conf *wuconf) {
	struct pcx_desc desc;
	struct wu_st st = pcx_open_file(&desc, infile->map);
	if (wu_isok(st)) {
		st = common_pcx(&desc, infile->sub_img, wuconf);
	}
	if (st.msg) {
		image_file_strerror_append(infile, st.msg);
	}
	return st.st;
}


static void dcx_end(struct image_file *infile) {
	dcx_free(infile->dec_state);
}

static enum wu_error dcx_callback(struct image_file *infile,
const struct wu_conf *wuconf, struct wu_state *state, const enum image_event ev) {
	if (ev == ev_subcycle) {
		struct dcx_desc *desc = infile->dec_state;
		const uint32_t i = (uint32_t)state->idx;
		struct wuimg *img = infile->sub_img + i;

		struct pcx_desc pcx;
		struct wu_st st = dcx_set_file(desc, &pcx, i);
		if (wu_isok(st)) {
			st = common_pcx(&pcx, img, wuconf);
		}
		if (st.msg) {
			image_file_strerror_append(infile, st.msg);
		}
		return st.st;
	}
	return wu_no_change;
}

static enum wu_error dcx_dec(struct image_file *infile,
const struct wu_conf *wuconf) {
	(void)wuconf;
	struct dcx_desc *desc = infile->dec_state;
	const struct wu_st st = dcx_open_file(desc, infile->map);
	if (wu_isok(st)) {
		return alloc_sub_images(infile, desc->nr) ? wu_ok : wu_alloc_error;
	}
	if (st.msg) {
		image_file_strerror_append(infile, st.msg);
	}
	return st.st;
}

const struct image_fn pcx_fn = {
	.mmap = true,
	.alloc_single = true,
	.dec = pcx_dec,
};
const struct image_fn dcx_fn = {
	.mmap = true,
	.state_size = sizeof(struct dcx_desc),
	.dec = dcx_dec,
	.callback = dcx_callback,
	.end = dcx_end,
};

// SPDX-License-Identifier: 0BSD
#include "wudefs.h"
#include "lib/atarist.h"

static void degas_end(struct image_file *infile) {
	struct degas_desc *desc = infile->dec_state;
	degas_free(desc);
	free(desc);
}

static enum wu_error degas_callback(struct image_file *infile,
const struct wu_conf *_c, struct wu_state *state, const enum image_event ev) {
	(void)_c;
	struct degas_desc *desc = infile->dec_state;
	struct wuimg *img = infile->sub_img;
	switch (ev) {
	case ev_subcycle:
		return degas_decode(desc, img) ? wu_ok : wu_decoding_error;
	case ev_time:
		palette_cycle_render(img->u.palette, desc->cycle, state->time);
		return wu_ok;
	default: break;
	}
	return wu_no_change;
}

static void res_metadata(struct wu_tree *meta, const enum atarist_res res) {
	tree_add_leaf_utf8(meta, "Resolution", atarist_res_str(res));
}

static void get_metadata(struct wu_tree *meta, const struct degas_desc *desc) {
	res_metadata(meta, desc->res);
	if (desc->is_elite) {
		tree_bud_leaf_bool(meta, "Elite", desc->is_elite);
	}
}

static enum wu_error degas_dec(struct image_file *infile,
const struct wu_conf *conf) {
	struct degas_desc *desc = malloc(sizeof(*desc));
	infile->dec_state = desc;
	enum wu_error st = wu_alloc_error;
	if (desc) {
		struct wuimg *img = alloc_sub_images(infile, 1);
		if (img) {
			st = degas_parse(desc, img, infile->ifp);
			if (st == wu_ok) {
				get_metadata(&infile->metadata, desc);
				st = wuimg_exceeds_limit(img, conf)
					? wu_exceeds_size_limit : wu_ok;
			}
		}
	}
	return st;
}

static void tiny_end(struct image_file *infile) {
	struct tiny_desc *desc = infile->dec_state;
	tiny_free(desc);
	free(desc);
}

static enum wu_error tiny_callback(struct image_file *infile,
const struct wu_conf *_c, struct wu_state *state, const enum image_event ev) {
	(void)_c;
	struct tiny_desc *desc = infile->dec_state;
	struct wuimg *img = infile->sub_img;
	switch (ev) {
	case ev_subcycle:
		return tiny_decode(desc, img) ? wu_ok : wu_decoding_error;
	case ev_time:
		palette_cycle_render(img->u.palette, desc->cycle, state->time);
		return wu_ok;
	default: break;
	}
	return wu_no_change;
}

static enum wu_error tiny_dec(struct image_file *infile,
const struct wu_conf *conf) {
	struct tiny_desc *desc = malloc(sizeof(*desc));
	infile->dec_state = desc;
	enum wu_error st = wu_alloc_error;
	if (desc) {
		struct wuimg *img = alloc_sub_images(infile, 1);
		if (img) {
			st = tiny_parse(desc, img, infile->map);
			if (st == wu_ok) {
				res_metadata(&infile->metadata, desc->res);
				tree_bud_leaf_u(&infile->metadata, "Iterations",
					desc->iters);
				st = wuimg_exceeds_limit(img, conf)
					? wu_exceeds_size_limit : wu_ok;
			}
		}
	}
	return st;
}

const struct image_fn degas_fn = {
	.mmap = false,
	.dec = degas_dec,
	.callback = degas_callback,
	.end = degas_end,
};

const struct image_fn tiny_fn = {
	.mmap = true,
	.dec = tiny_dec,
	.callback = tiny_callback,
	.end = tiny_end,
};

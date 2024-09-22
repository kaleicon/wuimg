// SPDX-License-Identifier: 0BSD
#include "wudefs.h"
#include "lib/degas.h"

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
		degas_color_cycle(desc, img, state->time);
		return wu_ok;
	default: break;
	}
	return wu_no_change;
}

static void get_metadata(struct wu_tree *meta, const struct degas_desc *desc) {
	tree_add_leaf_utf8(meta, "Resolution", degas_res_str(desc->res));
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
		st = degas_init(desc, infile->ifp);
		if (st == wu_ok) {
			struct wuimg *img = alloc_sub_images(infile, 1);
			if (img) {
				st = degas_parse(desc, img);
				if (st == wu_ok) {
					get_metadata(&infile->metadata, desc);
					return wuimg_exceeds_limit(img, conf)
						? wu_exceeds_size_limit : wu_ok;
				}
			} else {
				st = wu_alloc_error;
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

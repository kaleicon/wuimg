// SPDX-License-Identifier: 0BSD
#include "lib/bethesda.h"
#include "misc/math.h"
#include "wudefs.h"

static struct wu_st init_gxa(struct image_file *infile,
const struct wu_conf *conf) {
	struct gxa_desc desc;
	struct wu_st st = gxa_init(&desc, infile->ifp);
	if (wu_isok(st)) {
		if (alloc_sub_images(infile, desc.nb_images)) {
			tree_add_leaf_utf8_len(&infile->metadata, "Comment",
				wuptr_mem(desc.comment, desc.comment_len));
			size_t i = 0;
			while (i < desc.nb_images) {
				struct wuimg *img = infile->sub_img + i;
				st = gxa_next_image(&desc, img);
				if (!wu_isok(st)) {
					if (st.st == wu_no_change) {
						st.st = wu_ok;
					}
					break;
				}

				if (wuimg_exceeds_limit(img, conf)) {
					st = WUERR_HERE(wu_exceeds_size_limit);
					break;
				}

				st = gxa_load_image(&desc, img);
				if (!wu_isok(st)) {
					break;
				}
				++i;
			}
			st = wuerr(image_file_total_decoded(infile, i), st.msg);
		} else {
			st = WUERR_HERE(wu_alloc_error);
		}
	}
	gxa_cleanup(&desc);
	return st;
}

static void end_bsi(struct image_file *infile) {
	bsi_cleanup(infile->dec_state);
}

static struct wu_st event_bsi(struct image_file *infile,
const struct wu_conf *_c, struct wu_state *state, const enum image_event ev) {
	(void)_c;
	if (ev == ev_subcycle) {
		struct wuimg *img = infile->sub_img + state->idx;
		struct wu_st st = bsi_set_image(infile->dec_state, img);
		if (wu_isok(st)) {
			st = bsi_load_image(infile->dec_state, img,
				(uint16_t)state->idx);
		}
		return st;
	}
	return wuerr(wu_no_change, NULL);
}

static struct wu_st init_bsi(struct image_file *infile,
const struct wu_conf *conf) {
	struct bsi_desc *desc = infile->dec_state;
	struct wu_st st = bsi_init(desc, infile->ifp);
	if (wu_isok(st)) {
		if (umax(desc->w, desc->h) > conf->max_img_size) {
			st = WUERR_HERE(wu_exceeds_size_limit);
		} else if (!alloc_sub_images(infile, desc->nb_images)) {
			st = WUERR_HERE(wu_alloc_error);
		} else {
			tree_add_leaf_utf8(&infile->metadata, "Compression",
				bsi_compression_str(desc->compression));
			tree_add_leaf_utf8(&infile->metadata, "Type",
				desc->bsif ? "BSIF" : "IFHD");
		}
	}
	return st;
}

const struct image_fn gxa_fn = {
	.init = init_gxa,
};
const struct image_fn bsi_fn = {
	.state_size = sizeof(struct bsi_desc),
	.init = init_bsi,
	.event = event_bsi,
	.end = end_bsi,
};

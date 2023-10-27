// SPDX-License-Identifier: 0BSD
#include <string.h>

#include "wudefs.h"
#include "misc/file.h"
#include "lib/wpx.h"

static enum wu_error single_decode(struct wuimg *img,
const struct wu_conf *wuconf, struct wpx_bmp_desc *desc) {
	const enum wu_error status = wpx_bmp_parse(desc, img);
	if (status == wu_ok) {
		if (wuimg_exceeds_limit(img, wuconf)) {
			return wu_exceeds_size_limit;
		}
		return wpx_bmp_decode(desc, img) ? wu_ok : wu_decoding_error;
	}
	return status;
}

static enum wu_error wbm_dec(struct image_file *infile,
const struct wu_conf *wuconf) {
	struct wpx_bmp_desc desc;
	enum wu_error st = wpx_bmp_open(&desc, mp_map(infile->map));
	if (st == wu_ok) {
		tree_bud_leaf_u(&infile->metadata, "Depth", desc.depth);
		struct wuimg *img = alloc_sub_images(infile, 1);
		if (img) {
			st = single_decode(img, wuconf, &desc);
		} else {
			st = wu_alloc_error;
		}
		wpx_bmp_cleanup(&desc);
	}
	return st;
}


static void wia_end(struct image_file *infile) {
	wpx_ia2_cleanup(infile->dec_state);
	free(infile->dec_state);
}

static enum wu_error frame_decode(struct wpx_ia2_desc *desc, struct wuimg *img,
const struct wu_conf *wuconf, const uint32_t i) {
	struct wpx_bmp_desc frame;
	enum wu_error st = wpx_ia2_set_frame(desc, &frame, i);
	if (st == wu_ok) {
		st = single_decode(img, wuconf, &frame);
		wpx_bmp_cleanup(&frame);
	}
	return st;
}

static enum wu_error wia_callback(struct image_file *infile,
const struct wu_conf *wuconf, struct wu_state *state, const enum image_event ev) {
	(void)ev;
	struct wpx_ia2_desc *desc = infile->dec_state;
	struct wuimg *img = infile->sub_img + state->idx;
	return frame_decode(desc, img, wuconf, (uint32_t)state->idx);
}

static void add_list(struct wu_tree *tree, const char *branch_name,
const struct wpx_ia2_list *list) {
	struct wu_tree *br = NULL;
	for (uint32_t i = 0; i < list->idx.nr; ++i) {
		struct wuptr str;
		if (wpx_ia2_list_get(list, i, &str)) {
			if (!br) {
				br = tree_add_branch(tree, branch_name);
				if (!br) {
					return;
				}
			}
			char num[13];
			snprintf(num, sizeof(num), "%u", i);
			tree_add_leaf_len(br, num, str, NULL);
		}
	}
}

static void array_print(const char *name, const struct wpx_ia2_array *arr,
const uint32_t per_line) {
	if (arr->nr) {
		FILE *out = stderr;
		fputs(name, out);
		for (uint32_t i = 0; i < arr->nr; ++i) {
			if (i % per_line == 0) {
				fputc('\n', out);
			}
			fprintf(out, " %u", arr->val[i]);
		}
		fputc('\n', out);
	}
}

static void anim_metadata(struct wu_tree *tree, const struct wpx_ia2_desc *desc) {
	add_list(tree, "Names", &desc->names);
	add_list(tree, "SFX", &desc->sfx);
	if (0) {
		array_print("Mys3", &desc->mys3, 5);
		array_print("Mys4", &desc->mys4, 2);
		array_print("Mys5", &desc->mys5, 5);
	}
}

static enum wu_error wia_dec(struct image_file *infile,
const struct wu_conf *wuconf) {
	(void)wuconf;
	enum wu_error st = wu_alloc_error;
	struct wpx_ia2_desc *desc = calloc(1, sizeof(*desc));
	if (desc) {
		infile->dec_state = desc;
		infile->events = ev_subcycle;
		st = wpx_ia2_open(desc, mp_map(infile->map));
		if (st == wu_ok) {
			st = wpx_ia2_parse(desc);
			if (st == wu_ok) {
				anim_metadata(&infile->metadata, desc);
				if (!alloc_sub_images(infile, desc->frames.nr)) {
					st = wu_alloc_error;
				}
			}
		}
	}
	return st;
}

const struct image_fn wbm_fn = {.mmap = true, .dec = wbm_dec};
const struct image_fn wia_fn = {
	.mmap = true,
	.dec = wia_dec,
	.callback = wia_callback,
	.end = wia_end,
};

// SPDX-License-Identifier: 0BSD
#include <inttypes.h>

#include "lib/wpx.h"
#include "misc/math.h"
#include "wudefs.h"

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
	enum wu_error st = wpx_bmp_open(&desc, infile->map);
	if (st == wu_ok) {
		tree_bud_leaf_u(&infile->metadata, "Depth", desc.depth);
		st = single_decode(infile->sub_img, wuconf, &desc);
		wpx_bmp_cleanup(&desc);
	}
	return st;
}


static void wia_end(struct image_file *infile) {
	wpx_ia2_cleanup(infile->dec_state);
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
	struct wpx_ia2_desc *desc = infile->dec_state;
	struct wuimg *img = infile->sub_img + state->idx;
	return (ev == ev_subcycle)
		? frame_decode(desc, img, wuconf, (uint32_t)state->idx)
		: wu_no_change;
}

static void add_list(struct wu_tree *tree, const char *branch_name,
const struct wpx_ia2_list *list, const uint32_t nr) {
	struct wu_tree *br = NULL;
	const size_t m = zumin(nr, 1024);
	for (uint32_t i = 0; i < m; ++i) {
		struct wuptr str;
		if (wpx_ia2_list_get(list, i, &str)) {
			if (!br) {
				br = tree_add_branch(tree, branch_name);
				if (!br) {
					return;
				}
			}
			char num[13];
			snprintf(num, sizeof(num), "%" PRIu32, i);
			tree_add_leaf_len(br, num, str, NULL);
		}
	}
}

static void array_print(const char *name, const void *ptr,
const uint32_t nr, const size_t size) {
	FILE *out = stderr;
	fprintf(out, "%s (%u)\n", name, nr);
	const size_t fields = size/sizeof(uint32_t);
	for (uint32_t i = 0; i < nr; ++i) {
		for (size_t k = 0; k < fields; ++k) {
			const uint32_t *arr = ptr;
			fprintf(out, " %u", arr[i*fields + k]);
		}
		fputc('\n', out);
	}
}

static void anim_metadata(struct wu_tree *tree, const struct wpx_ia2_desc *desc) {
	add_list(tree, "Names", &desc->names, desc->nr.frames);
	add_list(tree, "SFX", &desc->sfx, desc->nr.sfx);
	const bool debug = false;
	if (debug) {
		array_print("Geom", desc->geom, desc->nr.geom, sizeof(*desc->geom));
		array_print("Range", desc->range, desc->nr.range, sizeof(*desc->range));
		array_print("Mys5", desc->mys5, desc->nr.mys5, sizeof(*desc->mys5));
	}
}

static enum wu_error wia_dec(struct image_file *infile,
const struct wu_conf *wuconf) {
	(void)wuconf;
	struct wpx_ia2_desc *desc = infile->dec_state;
	enum wu_error st = wpx_ia2_open(desc, infile->map);
	if (st == wu_ok) {
		st = wpx_ia2_parse(desc);
		if (st == wu_ok) {
			anim_metadata(&infile->metadata, desc);
			if (!alloc_sub_images(infile, desc->nr.frames)) {
				st = wu_alloc_error;
			}
		}
	}
	return st;
}

const struct image_fn wbm_fn = {
	.mmap = true,
	.alloc_single = true,
	.dec = wbm_dec,
};
const struct image_fn wia_fn = {
	.mmap = true,
	.state_size = sizeof(struct wpx_ia2_desc),
	.dec = wia_dec,
	.callback = wia_callback,
	.end = wia_end,
};

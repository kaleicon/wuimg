// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2022 kaleido
#include <inttypes.h>

#include "lib/wpx.h"
#include "misc/math.h"
#include "wudefs.h"

static struct wu_st single_decode(struct wuimg *img,
const struct wu_conf *wuconf, const struct wuptr mem) {
	struct wpx_bmp_desc desc;
	struct wu_st status = wpx_bmp_parse(&desc, mem, img);
	if (wu_isok(status)) {
		enum wu_error e = wuimg_alloc_limit(img, wuconf);
		if (e == wu_ok) {
			status = wpx_bmp_decode(&desc, img);
		} else {
			status = WUERR_HERE(e);
		}
	}
	wpx_bmp_cleanup(&desc);
	return status;
}

static struct wu_st init_wbm(struct image_file *infile,
const struct wu_conf *wuconf) {
	return single_decode(infile->sub_img, wuconf, infile->map);
}


static void end_wia(struct image_file *infile) {
	wpx_ia2_cleanup(infile->dec_state);
}

static struct wu_st event_wia(struct image_file *infile,
const struct wu_conf *wuconf, struct wu_state *state, const enum image_event ev) {
	struct wpx_ia2_desc *desc = infile->dec_state;
	struct wuimg *img = infile->sub_img + state->idx;
	struct wu_st st = WU_NO_CHANGE;
	if (ev == ev_subcycle) {
		st = single_decode(img, wuconf,
			wpx_ia2_get_frame(desc, (uint32_t)state->idx));
	}
	return st;
}

static void add_list(struct wutree *tree, const char *branch_name,
const struct wpx_ia2_list *list, const uint32_t nr) {
	struct wutree *br = NULL;
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

static void anim_metadata(struct wutree *tree, const struct wpx_ia2_desc *desc) {
	add_list(tree, "Names", &desc->names, desc->nr.frames);
	add_list(tree, "SFX", &desc->sfx, desc->nr.sfx);
	const bool debug = false;
	if (debug) {
		array_print("Geom", desc->geom, desc->nr.geom, sizeof(*desc->geom));
		array_print("Range", desc->range, desc->nr.range, sizeof(*desc->range));
		array_print("Mys5", desc->mys5, desc->nr.mys5, sizeof(*desc->mys5));
	}
}

static struct wu_st init_wia(struct image_file *infile,
const struct wu_conf *wuconf) {
	(void)wuconf;
	struct wpx_ia2_desc *desc = infile->dec_state;
	struct wu_st st = wpx_ia2_parse(desc, infile->map);
	if (wu_isok(st)) {
		anim_metadata(&infile->metadata, desc);
		infile->nr = desc->nr.frames;
	}
	return st;
}

const struct image_fn wbm_fn = {
	.mmap = true,
	.alloc_single = true,
	.init = init_wbm,
};
const struct image_fn wia_fn = {
	.mmap = true,
	.state_size = sizeof(struct wpx_ia2_desc),
	.init = init_wia,
	.event = event_wia,
	.end = end_wia,
};

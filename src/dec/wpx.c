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

static enum wu_error decode(struct image_file *infile,
const struct wu_conf *wuconf, struct wpx_bmp_desc *desc) {
	struct wuimg *img = alloc_sub_images(infile, 1);
	if (!img) {
		return wu_alloc_error;
	}

	enum wu_error st = single_decode(img, wuconf, desc);
	if (st == wu_ok) {
		const struct wu_leaf leaf = {
			wu_leaf_unsigned, {.u = desc->depth},
		};
		tree_bud_leaf(&infile->metadata, "Depth", leaf);
	}
	return st;
}

enum wu_error wbm_dec(struct image_file *infile, const struct wu_conf *wuconf) {
	struct map_info mm;
	if (file_map(&mm, infile->ifp)) {
		struct wpx_bmp_desc desc;
		enum wu_error st = wpx_bmp_open(&desc, mp_parser_map(mm));
		if (st == wu_ok) {
			st = decode(infile, wuconf, &desc);
			wpx_bmp_cleanup(&desc);
		}
		file_unmap(&mm);
		return st;
	}
	return wu_open_error;
}


struct wia_state {
	struct map_info mm;
	struct wpx_ia2_desc desc;
};

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

enum wu_error wia_callback(struct image_file *infile,
const struct wu_conf *wuconf, struct wu_state *state, const enum image_event ev) {
	struct wia_state *ds = infile->dec_state;
	if (ev == ev_subcycle) {
		struct wuimg *img = infile->sub_img + state->idx;
		if (!img->data) {
			return frame_decode(&ds->desc, img, wuconf,
				(uint32_t)state->idx);
		}
	} else {
		wpx_ia2_cleanup(&ds->desc);
		file_unmap(&ds->mm);
	}
	return wu_no_change;
}

static void add_list(struct wu_tree *tree, const char *branch_name,
const struct wpx_ia2_list *list) {
	if (list->idx.nr) {
		struct wu_tree *br = tree_add_branch(tree, branch_name);
		if (br) {
			for (uint32_t i = 0; i < list->idx.nr; ++i) {
				char num[13];
				snprintf(num, sizeof(num), "%u", i);
				tree_add_leaf(br, num,
					(char *)list->str + list->idx.val[i], NULL);
			}
		}
	}
}
__attribute__((unused))
static void array_print(const char *name, const struct wpx_ia2_array *arr,
const uint32_t space) {
	if (arr->nr) {
		FILE *out = stderr;
		fputs(name, out);
		for (uint32_t i = 0; i < arr->nr; ++i) {
			if (i % space == 0) {
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
//	array_print("Mys3", &desc->mys3, 5);
//	array_print("Mys4", &desc->mys4, 2);
//	array_print("Mys5", &desc->mys5, 5);
}

static enum wu_error anim_setup(struct image_file *infile,
const struct wu_conf *wuconf, struct wpx_ia2_desc *desc) {
	enum wu_error status = wpx_ia2_parse(desc);
	if (status != wu_ok) {
		return status;
	}
	anim_metadata(&infile->metadata, desc);
	if (!alloc_sub_images(infile, desc->frames.nr)) {
		return wu_alloc_error;
	}
	return frame_decode(desc, infile->sub_img, wuconf, 0);
}

enum wu_error wia_dec(struct image_file *infile, const struct wu_conf *wuconf) {
	enum wu_error st = wu_alloc_error;
	struct wia_state *ds = calloc(1, sizeof(*ds));
	if (ds) {
		if (file_map(&ds->mm, infile->ifp)) {
			st = wpx_ia2_open(&ds->desc, mp_parser_map(ds->mm));
			if (st == wu_ok) {
				infile->dec_state = ds;
				infile->events = ev_subcycle;
				return anim_setup(infile, wuconf, &ds->desc);
			}
			file_unmap(&ds->mm);
		} else {
			st = wu_open_error;
		}
		free(ds);
	}
	return st;
}

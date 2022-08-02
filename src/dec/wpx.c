#include <string.h>

#include "wudefs.h"
#include "wutree.h"
#include "common.h"
#include "lib/wpx.h"

static enum wu_error single_decode(struct raw_img *img,
const struct wu_conf *wuconf, struct wpx_bmp_desc *desc) {
	const enum wu_error status = wpx_bmp_parse(desc, img);
	if (status == wu_ok) {
		if (raw_img_exceeds_limit(img, wuconf)) {
			return wu_exceeds_size_limit;
		}
		return wpx_bmp_decode(desc, img) ? wu_ok : wu_decoding_error;
	}
	return status;
}

static enum wu_error decode(struct image_file *infile,
const struct wu_conf *wuconf, struct wpx_bmp_desc *desc) {
	struct raw_img *img = alloc_sub_images(infile, 1);
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
	if (map_file(&mm, infile->ifp)) {
		struct wpx_bmp_desc desc;
		enum wu_error st = wpx_bmp_open(&desc, &mm);
		if (st == wu_ok) {
			st = decode(infile, wuconf, &desc);
			wpx_bmp_cleanup(&desc);
		}
		unmap_file(&mm);
		return st;
	}
	return wu_open_error;
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
					(char *)list->str + list->idx.val[i]);
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

static enum wu_error anim_wrap(struct image_file *infile,
const struct wu_conf *wuconf, struct wpx_ia2_desc *desc) {
	enum wu_error status = wpx_ia2_parse(desc);
	if (status != wu_ok) {
		return status;
	}

	anim_metadata(&infile->metadata, desc);
	if (!alloc_sub_images(infile, desc->frames.nr)) {
		return wu_alloc_error;
	}

	uint32_t o = 0;
	for (uint32_t i = 0; i < desc->frames.nr; ++i) {
		struct wpx_bmp_desc frame;
		status = wpx_ia2_set_frame(desc, &frame, i);
		if (status == wu_ok) {
			struct raw_img *img = infile->sub_img + o;
			if (single_decode(img, wuconf, &frame)) {
				++o;
			}
			wpx_bmp_cleanup(&frame);
		}
	}
	return image_file_total_decoded(infile, o);
}

enum wu_error wia_dec(struct image_file *infile, const struct wu_conf *wuconf) {
	struct map_info mm;
	if (map_file(&mm, infile->ifp)) {
		struct wpx_ia2_desc desc;
		enum wu_error st = wpx_ia2_open(&desc, &mm);
		if (st == wu_ok) {
			st = anim_wrap(infile, wuconf, &desc);
			wpx_ia2_cleanup(&desc);
		}
		unmap_file(&mm);
		return st;
	}
	return wu_open_error;
}

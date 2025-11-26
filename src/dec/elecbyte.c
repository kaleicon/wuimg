// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2025 kaleido
#include <string.h>

#include "lib/elecbyte.h"
#include "wudefs.h"

#include "dec_enable.def"
#include "dec.h"
#include "dec_fn.h"

static void add_version(struct wutree *tree, const uint8_t version[static 4]) {
	char str[16];
	tree_add_leaf_utf8_len(tree, "Version",
		wuptr_mem(str, eb_print_version(str, version)));
}

static void add_metadata(struct wuimg *img, const struct eb_sff_desc *desc,
const struct eb_sff_sub *sub) {
	struct wutree *meta = wuimg_get_metadata(img);
	if (meta) {
		const uint8_t version = eb_sff_get_version(desc);
		meta = tree_add_branch(meta, version == 2 ? "SFF2" : "SFF");
		if (meta) {
			switch (version) {
			case 1:
				;const struct eb_sff1_sub *sub1 = &sub->u.v1;
				tree_bud_leaf_d(meta, "X", sub1->x);
				tree_bud_leaf_d(meta, "Y", sub1->y);
				tree_bud_leaf_d(meta, "Group", sub1->group);
				tree_bud_leaf_d(meta, "Image", sub1->image);
				tree_bud_leaf_bool(meta, "Shared palette",
					sub1->shared_pal);
				tree_add_leaf_len(meta, "Comment", sub1->comm,
					NULL);
				break;
			case 2:
				;const struct eb_sff2_sub *sub2 = &sub->u.v2;
				tree_bud_leaf_u(meta, "Group", sub2->group);
				tree_bud_leaf_u(meta, "Item", sub2->item);
				tree_bud_leaf_u(meta, "X", sub2->x);
				tree_bud_leaf_u(meta, "Y", sub2->y);
				tree_add_leaf_utf8(meta, "Format",
					eb_sff2_format_str(sub2->fmt));
				tree_bud_leaf_u(meta, "Depth", sub2->depth);
				break;
			}
		}
	}
}

static void img_swap(struct wuimg *dst, struct wuimg *restrict src,
struct wuimg *first, const struct eb_sff_desc *desc, const struct eb_sff_sub *sub) {
	memcpy(dst, src, sizeof(*src));
	memset(src, 0, sizeof(*src));
	eb_sff_touchup(desc, sub, dst, first);
}

static enum wu_error sff_loop(struct image_file *infile,
const struct wu_conf *conf, struct wuimg *base, struct eb_sff_desc *desc) {
	struct wudec_image ctx = {.conf = *conf};
	enum wu_error st = wu_ok;
	size_t o = 0;
	for (size_t i = 0; i < infile->nr; ++i) {
		struct eb_sff_sub sub;
		st = eb_sff_next(desc, &sub);
		if (st != wu_ok) {
			continue;
		}

		struct wuimg *img = base + o;
		const enum eb_sff_decoder which = eb_sff_get_decoder(desc, &sub);
		if (which == eb_sff_sff) {
			st = eb_sff2_get_dims(&sub, img);
			if (st == wu_ok) {
				if (!wuimg_exceeds_limit(img, conf)) {
					if (eb_sff2_dec(&sub, img)) {
						add_metadata(img, desc, &sub);
						++o;
					} else {
						st = wu_decoding_error;
					}
				} else {
					st = wu_exceeds_size_limit;
				}
			}
			if (st != wu_ok) {
				wuimg_clear(img);
			}
		} else {
			const struct image_fn *fn = NULL;
			const char *failmsg = NULL;
			switch (which) {
			case eb_sff_pcx:
#ifdef WU_ENABLE_PCX
				fn = &pcx_fn;
#else
				failmsg = "pcx decoder not compiled";
#endif
				break;
			case eb_sff_png:
#ifdef WU_ENABLE_PNG
				fn = &png_fn;
#else
				failmsg = "png decoder not compiled";
#endif
				break;
			default: failmsg = "unreachable case reached"; break;
			}
			if (fn) {
				wudec_src_mem(&ctx, sub.data, NULL, fn);
				st = wudec_decode(&ctx);
				if (st == wu_ok) {
					img_swap(img, ctx.file.sub_img, base, desc, &sub);
					add_metadata(img, desc, &sub);
					++o;
				}
				wudec_recycle(&ctx);
			} else {
				image_file_strerror_append(infile, failmsg);
				st = wu_unsupported_feature;
			}
		}
	}
	eb_sff_cleanup(desc);
	if (st != wu_ok && st != wu_no_change) {
		image_file_error_append(infile, st);
	}
	return image_file_total_decoded(infile, o);
}

static enum wu_error eb_sff_dec(struct image_file *infile,
const struct wu_conf *conf) {
	struct eb_sff_desc desc;
	enum wu_error st = eb_sff_init(&desc, infile->map);
	if (st == wu_ok) {
		st = eb_sff_parse(&desc);
		if (st == wu_ok) {
			struct wuimg *img = alloc_sub_images(infile, desc.images);
			if (img) {
				struct wutree *meta = &infile->metadata;
				add_version(meta, desc.version);
				tree_bud_leaf_u(meta, "Images", desc.images);
				switch (eb_sff_get_version(&desc)) {
				case 1:
					tree_bud_leaf_u(meta, "Groups", desc.u.v1.groups);
					break;
				case 2:
					tree_bud_leaf_u(meta, "Palettes", desc.u.v2.pal_nr);
					break;
				}
				tree_add_leaf_len(meta, "Comment", desc.comm,
					NULL);
				return sff_loop(infile, conf, img, &desc);
			}
			return wu_alloc_error;
		}
	}
	return st;
}

static enum wu_error eb_fnt_dec(struct image_file *infile,
const struct wu_conf *conf) {
#ifdef WU_ENABLE_PCX
	struct eb_fnt_desc desc;
	enum wu_error st = eb_fnt_init(&desc, infile->map);
	if (st == wu_ok) {
		st = eb_fnt_parse(&desc);
		if (st == wu_ok) {
			struct wutree *tree = &infile->metadata;
			add_version(tree, desc.version);
			tree_add_leaf_len(tree, "Comment", desc.comment, NULL);
			tree_add_leaf_len(tree, "Font definition", desc.text,
				NULL);

			struct wudec_image ctx = {.conf = *conf};
			wudec_src_mem(&ctx, desc.pcx, NULL, &pcx_fn);
			st = wudec_decode(&ctx);

			infile->sub_img = ctx.file.sub_img;
			infile->nr = ctx.file.nr;
			ctx.file.sub_img = NULL;
			ctx.file.nr = 0;
			wudec_free(&ctx);
			if (infile->sub_img->mode == image_mode_palette) {
				infile->sub_img->u.palette->color[0].a = 0;
			}
		}
	}
	return st;
#else
	(void)conf;
	image_file_strerror_append(infile, "pcx decoder not compiled");
	return wu_unsupported_feature;
#endif
}

const struct image_fn eb_sff_fn = {
	.mmap = true,
	.dec = eb_sff_dec,
};

const struct image_fn eb_fnt_fn = {
	.mmap = true,
	.dec = eb_fnt_dec,
};

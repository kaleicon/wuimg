#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "../wudefs.h"
#include "../lib/pcx.h"

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

static void add_metadata(struct wu_tree *metadata,
const struct pcx_desc *desc, struct raw_img *img) {
	const char ver_fmt[] = "%hhu (%s)";
	char buf[sizeof(ver_fmt) + 20];
	const size_t w = (size_t)sprintf(buf, ver_fmt, desc->version,
		pcx_version_string(desc->version));
	tree_sprout_measured_leaf(metadata, "Format version", buf, w);

	struct wu_leaf leaf = {
		.val.u = img->channels,
		.type = wu_leaf_unsigned
	};
	tree_bud_leaf(metadata, "Planes", leaf);

	leaf.val.u = img->bitdepth;
	tree_bud_leaf(metadata, "Bitdepth", leaf);

	leaf.val.u = desc->palette_type;
	tree_bud_leaf(metadata, "Palette mode", leaf);

	if (desc->entries <= 4) {
		const void *garbage = desc->file_pal + 12;
		const size_t len = is_readable_garbage(garbage,
			sizeof(desc->file_pal) - 12);
		if (len > 3) {
			tree_sprout_measured_leaf(metadata, "Garbage", garbage,
				len);
		}
	}
}

static enum wu_error common_pcx(struct pcx_desc *desc, struct raw_img *img,
const struct wu_conf *wuconf, struct wu_tree *metadata) {
	const enum wu_error st = pcx_read_header(desc, img);
	if (st == wu_ok) {
		if (metadata) {
			add_metadata(metadata, desc, img);
		}
		if (raw_img_exceeds_limit(img, wuconf)) {
			return wu_exceeds_size_limit;
		}
		return pcx_decode(desc, img) ? wu_ok : wu_decoding_error;
	}
	return st;
}

enum wu_error pcx_dec(struct image_file *infile, const struct wu_conf *wuconf) {
	enum wu_error err = wu_open_error;
	struct map_info mm;
	if (map_file(&mm, infile->ifp)) {
		struct pcx_desc desc;
		err = pcx_open_file(&desc, &mm);
		if (err == wu_ok) {
			struct raw_img *img = alloc_sub_images(infile, 1);
			if (img) {
				err = common_pcx(&desc, img, wuconf,
					&infile->metadata);
			} else {
				err = wu_alloc_error;
			}
		}
		unmap_file(&mm);
	}
	return err;
}

static enum wu_error wrap_dcx(struct image_file *infile,
const struct wu_conf *wuconf, const struct dcx_desc *dcx) {
	struct raw_img *img = alloc_sub_images(infile, dcx->nr);
	if (!img) {
		return wu_alloc_error;
	}

	size_t dec = 0;
	for (uint32_t i = 0; i < dcx->nr; ++i) {
		struct pcx_desc pcx;
		if (dcx_set_file(dcx, &pcx, i) == wu_ok
		&& common_pcx(&pcx, img + dec, wuconf, &infile->metadata) == wu_ok) {
			++dec;
		} else {
			raw_img_clear(img + dec);
		}
	}
	return image_file_total_decoded(infile, dec);
}

enum wu_error dcx_dec(struct image_file *infile, const struct wu_conf *wuconf) {
	struct map_info mm;
	if (map_file(&mm, infile->ifp)) {
		struct dcx_desc desc;
		enum wu_error st = dcx_open_file(&desc, &mm);
		if (st == wu_ok) {
			st = wrap_dcx(infile, wuconf, &desc);
			dcx_free(&desc);
		}
		unmap_file(&mm);
		return st;
	}
	return wu_open_error;
}

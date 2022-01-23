#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "../common.h"
#include "../wudefs.h"
#include "../rast_utils.h"
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
const struct pcx_desc *desc) {
	const char ver_fmt[] = "%hhu (%s)";
	char buf[sizeof(ver_fmt) + 20];
	const size_t w = (size_t)sprintf(buf, ver_fmt, desc->version,
		pcx_version_string(desc->version));
	tree_sprout_measured_leaf(metadata, "Format version", buf, w);

	struct wu_leaf leaf = {
		.val.u = desc->r.ch,
		.type = wu_leaf_unsigned
	};
	tree_bud_leaf(metadata, "Planes", leaf);

	leaf.val.u = desc->r.bitdepth;
	tree_bud_leaf(metadata, "Bitdepth", leaf);

	leaf.val.u = desc->palette_type;
	tree_bud_leaf(metadata, "Palette mode", leaf);

	if (desc->entries <= 4) {
		const void *garbage = desc->file_pal + 12;
		const size_t len = is_readable_garbage(garbage,
			sizeof(desc->file_pal) - 12);
		if (len) {
			tree_sprout_measured_leaf(metadata, "Garbage", garbage,
				len);
		}
	}
}

static enum wu_error common_pcx(FILE *ifp, struct raw_img *img,
struct wu_tree *metadata, const struct wu_conf *wuconf, const long file_len) {
	struct pcx_desc desc;
	enum lib_fail status = pcx_open_file(ifp, &desc, file_len);
	if (status != lib_ok) {
		return wu_unknown_file_type;
	}

	status = pcx_read_header(&desc);
	if (status != lib_ok) {
		return wu_invalid_header;
	}

	if (metadata) {
		add_metadata(metadata, &desc);
	}

	if (rast_exceeds_size(&desc.r, wuconf)) {
		return wu_exceeds_size_limit;
	}

	img->data = pcx_decode(&desc);
	rast_to_raw(img, &desc.r);
	return img->data ? wu_ok : wu_alloc_error;
}

enum wu_error pcx_dec(struct image_file *infile, const struct wu_conf *wuconf) {
	struct raw_img *img = alloc_sub_images(infile, 1);
	if (!img) {
		return wu_alloc_error;
	}
	return common_pcx(infile->ifp, img, &infile->metadata, wuconf, 0);
}

enum wu_error dcx_dec(struct image_file *infile, const struct wu_conf *wuconf) {
	const enum lib_fail status = dcx_open_file(infile->ifp);
	if (status != lib_ok) {
		return wu_unknown_file_type;
	}

	struct dcx_desc *desc = dcx_read_offsets(infile->ifp);
	if (!desc) {
		return wu_alloc_error;
	} else if (!desc->nr) {
		return wu_unexpected_eof;
	}

	size_t max = 0;
	for (size_t i = 0; i < desc->nr; ++i) {
		max = zumax(max, desc->len[i]);
	}
	if (max <= 128) {
		free(desc);
		return wu_unexpected_eof;
	}

	struct raw_img *img = alloc_sub_images(infile, desc->nr);
	if (!img) {
		free(desc);
		return wu_alloc_error;
	}

	size_t decoded = 0;
	for (size_t i = 0; i < desc->nr; ++i) {
		fseek(infile->ifp, (long)desc->off[i], SEEK_SET);
		const enum wu_error res = common_pcx(infile->ifp,
			img + decoded, NULL, wuconf, (long)desc->len[i]);
		if (res == wu_ok) {
			++decoded;
		}
	}
	free(desc);
	return image_file_total_decoded(infile, decoded);
}

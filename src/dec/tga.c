#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>

#include "../wudefs.h"
#include "../common.h"

#include "../lib/tga.h"

static void read_extension_area(struct wu_tree *tree,
const struct tga_metadata *meta) {
	tree_add_leaf_len(tree, "Author name", meta->author.name,
		sizeof(meta->author.name), NULL);
	tree_add_leaf_len(tree, "Author comment", meta->author.comment,
		sizeof(meta->author.comment), NULL);

	struct wu_leaf leaf;
	if (meta->timestamp) {
		leaf.val.time = meta->timestamp;
		leaf.type = wu_leaf_time;
		tree_bud_leaf(tree, "Timestamp", leaf);
	}

	tree_add_leaf_len(tree, "Job ID", meta->job.name,
		sizeof(meta->job.name), NULL);

	if (meta->job.hour || meta->job.minute || meta->job.second) {
		const char fmt[] = "%.2hu:%.2hu:%.2hu";
		char buf[sizeof(fmt)];
		const size_t w =(size_t)snprintf(buf, sizeof(buf), fmt,
			meta->job.hour, meta->job.minute, meta->job.second);
		tree_add_leaf_utf8_len(tree, "Job time", buf, w);
	}

	tree_add_leaf_len(tree, "Software ID", meta->software.id,
		sizeof(meta->software.id), NULL);

	if (isgraph(meta->software.version_letter)) {
		tree_add_leaf_utf8_len(tree, "Software version letter",
			&meta->software.version_letter, 1);
	}
	if (meta->software.version_number) {
		leaf.val.u = meta->software.version_number;
		leaf.type = wu_leaf_unsigned;
		tree_bud_leaf(tree, "Software version number", leaf);
	}
}

static void read_tga_info(struct wu_tree *tree, const struct tga_desc *desc) {
	tree_add_leaf_utf8(tree, "Type", tga_type_str(desc->type));
	struct wu_leaf leaf = {.type = wu_leaf_unsigned, .val.u = desc->depth};
	tree_bud_leaf(tree, "Depth", leaf);
	if (desc->map.depth) {
		leaf.val.u = desc->map.depth;
		tree_bud_leaf(tree, "Map depth", leaf);
	}
	tree_add_leaf_len(tree, "ID", desc->meta.id, desc->meta.id_len, NULL);
}

static enum wu_error dec_wrapper(struct image_file *infile,
const struct wu_conf *wuconf, struct tga_desc *desc) {
	struct raw_img *img = alloc_sub_images(infile, 1);
	if (!img) {
		return wu_alloc_error;
	}

	enum wu_error st = tga_parse_header(desc, img, infile->ifp);
	if (st) {
		return st;
	}

	read_tga_info(&infile->metadata, desc);
	bool extra_pal = (bool)desc->map.extra_pal;
	bool has_stamp = false;
	if (tga_parse_footer(desc, img)) {
		struct wu_tree *extra = tree_add_branch(&infile->metadata,
			"Extension area");
		read_extension_area(extra, &desc->meta);
		infile->bg = desc->meta.key_color;
		if (desc->meta.stamp_offset) {
			has_stamp = true;
		}
	}

	img = realloc_sub_images(infile, 1u + extra_pal + has_stamp);
	if (!img) {
		return wu_alloc_error;
	}

	size_t i = 1;
	if (has_stamp) {
		st = tga_parse_stamp(desc, img, img + i);
		if (st != wu_ok) {
			return st;
		}
		++i;
	}
	if (extra_pal) {
		img[i].data = (uint8_t *)tga_take_extra_palette(desc);
		img[i].w = 16;
		img[i].h = 16;
		img[i].channels = 4;
		img[i].bitdepth = 8;
	}
	for (i = 0; i < infile->nr; ++i) {
		if (raw_img_exceeds_limit(img + i, wuconf)) {
			return wu_exceeds_size_limit;
		}
	}

	if (!tga_decode(desc, img)) {
		return wu_decoding_error;
	}
	if (has_stamp) {
		if (!tga_decode_stamp(desc, img + 1)) {
			return wu_decoding_error;
		}
	}
	return wu_ok;
}

enum wu_error tga_dec(struct image_file *infile, const struct wu_conf *wuconf) {
	struct tga_desc desc;
	const enum wu_error err = dec_wrapper(infile, wuconf, &desc);
	tga_cleanup(&desc);
	return err;
}

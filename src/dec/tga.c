#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>

#include "../wudefs.h"
#include "../common.h"
#include "../rast_utils.h"

#include "../lib/tga.h"

static void read_extension_area(struct wu_tree *tree,
const struct tga_metadata *meta) {
	tree_sprout_unsafe_leaf(tree, "Author name", meta->author.name,
		sizeof(meta->author.name));
	tree_sprout_unsafe_leaf(tree, "Author comment", meta->author.comment,
		sizeof(meta->author.comment));

	struct wu_leaf leaf;
	if (meta->has_timestamp) {
		leaf.val.time = meta->timestamp;
		leaf.type = wu_leaf_time;
		tree_bud_leaf(tree, "Timestamp", leaf);
	}

	tree_sprout_unsafe_leaf(tree, "Job ID", meta->job.name,
		sizeof(meta->job.name));

	if (meta->job.hour || meta->job.minute || meta->job.second) {
		const char fmt[] = "%.2hu:%.2hu:%.2hu";
		char buf[sizeof(fmt)];
		const size_t w =(size_t)sprintf(buf, fmt,
			meta->job.hour, meta->job.minute, meta->job.second);
		tree_sprout_measured_leaf(tree, "Job time", buf, w);
	}

	tree_sprout_unsafe_leaf(tree, "Software ID", meta->software.id,
		sizeof(meta->software.id));

	if (isgraph(meta->software.version_letter)) {
		tree_sprout_measured_leaf(tree, "Software version letter",
			&meta->software.version_letter, 1);
	}
	if (meta->software.version_number) {
		leaf.val.u = meta->software.version_number;
		leaf.type = wu_leaf_unsigned;
		tree_bud_leaf(tree, "Software version number", leaf);
	}
}

static void read_tga_info(struct wu_tree *tree, const struct tga_desc *desc) {
	struct wu_leaf leaf = {.type = wu_leaf_unsigned, .val.u = desc->depth};
	tree_bud_leaf(tree, "Depth", leaf);
	leaf.val.u = desc->attr_bits;
	tree_bud_leaf(tree, "Attribute bits", leaf);
	if (desc->r.palette) {
		leaf.val.u = desc->map.depth;
		tree_bud_leaf(tree, "Map depth", leaf);
	}
	tree_sprout_unsafe_leaf(tree, "ID", desc->meta.id, desc->meta.id_len);
}

static enum wu_error dec_wrapper(struct image_file *infile,
const struct wu_conf *wuconf, struct tga_desc *desc) {
	enum lib_fail fail = tga_parse_header(desc, infile->ifp);
	if (fail) {
		rast_error(infile, fail);
		return wu_alloc_error;
	}

	if (rast_exceeds_size(&desc->r, wuconf)) {
		return wu_exceeds_size_limit;
	}

	read_tga_info(&infile->metadata, desc);
	if (tga_parse_footer(desc)) {
		struct wu_tree *extra = tree_sprout_branch(&infile->metadata,
			"Extension area");
		read_extension_area(extra, &desc->meta);
		infile->bg = desc->meta.key_color;
	}

	const bool has_stamp = desc->meta.stamp_offset;
	struct raw_img *img = alloc_sub_images(infile,
		1U + (bool)desc->map.pal + has_stamp);
	if (!img) {
		return wu_alloc_error;
	}

	int i = 0;
	if (!rast_to_raw_img(&desc->r, img + i)) {
		return wu_alloc_error;
	}
	img[i].disable_alpha = !desc->attr_bits;
	img[i].mirror = !(desc->orientation & 0x02);
	img[i].rotate = (unsigned char)((desc->orientation & 0x01) * 2);
	if (!tga_decode(desc, img[i].data)) {
		return wu_decoding_error;
	}

	if (desc->map.pal) {
		++i;
		img[i].data = (unsigned char *)tga_take_extra_palette(desc);
		img[i].id = strdup("extra_palette");
		img[i].w = desc->map.len;
		img[i].h = 1;
		img[i].bitdepth = 8;
		img[i].channels = 4;
		img[i].layout = pix_bgra;
	}

	if (has_stamp) {
		++i;
		if (rast_to_raw_img(&desc->meta.stamp, img + i)
		&& tga_decode_stamp(desc, img[i].data)) {
			img[i].id = strdup("stamp");
		} else {
			realloc_sub_images(infile, infile->nr - 1);
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

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
		leaf.val.time = utc_to_epoch(&meta->timestamp);
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
	tree_sprout_unsafe_leaf(tree, "ID", desc->meta->id, desc->meta->id_len);
}

enum wu_error tga_dec(struct image_file *infile, const struct wu_conf *wuconf) {
	struct tga_desc desc;
	enum lib_fail fail = tga_open_file(infile->ifp, &desc, true);
	if (fail) {
		infile->err_msg = strdup(lib_fail_string(fail));
		return wu_alloc_error;
	}

	fail = tga_parse_header(&desc);
	if (fail) {
		tga_cleanup(&desc);
		infile->err_msg = strdup(lib_fail_string(fail));
		return wu_invalid_header;
	}

	if (rast_exceeds_size(&desc.r, wuconf)) {
		tga_cleanup(&desc);
		return wu_exceeds_size_limit;
	}

	read_tga_info(&infile->metadata, &desc);
	if (tga_parse_footer(&desc)) {
		struct wu_tree *extra = tree_sprout_branch(&infile->metadata,
			"Extension area data");
		read_extension_area(extra, desc.meta);
		infile->bg = desc.meta->key_color;
	}

	const bool extra_palette = desc.map.pal;
	const bool stamp = desc.meta && desc.meta->stamp_offset;

	struct raw_img *img = alloc_sub_images(infile,
		1U + extra_palette + stamp);
	if (!img) {
		tga_cleanup(&desc);
		return wu_alloc_error;
	}

	img[0].data = tga_decode(&desc);
	if (!img[0].data) {
		tga_cleanup(&desc);
		return wu_decoding_error;
	}

	rast_to_raw(img, &desc.r);
	img[0].no_alpha = !desc.attr_bits;
	img[0].mirror = !(desc.orientation & 0x02);
	img[0].rotate = (unsigned char)((desc.orientation & 0x01) * 2);

	int i = 0;
	if (extra_palette) {
		++i;
		img[i].data = (unsigned char *)tga_take_extra_palette(&desc);
		img[i].id = strdup("extra_palette");
		img[i].w = desc.map.len;
		img[i].h = 1;
		img[i].bitdepth = 8;
		img[i].channels = 4;
		img[i].layout = pix_bgra;
	}

	if (stamp) {
		++i;
		size_t swidth, sheight;
		img[i].data = tga_decode_stamp(&desc, &swidth, &sheight);
		if (!img[i].data) {
			realloc_sub_images(infile, infile->nr - 1);
		} else {
			img[i] = img[0];
			img[i].palette = NULL;
			img[i].id = strdup("stamp");
			img[i].w = swidth;
			img[i].h = sheight;

			if (img[0].palette) {
				img[i].palette = memdup(img[0].palette,
					sizeof(*img[0].palette));
				if (!img[i].palette) {
					realloc_sub_images(infile,
						infile->nr - 1);
					tga_cleanup(&desc);
					return wu_ok;
				}
			}
		}
	}

	tga_cleanup(&desc);
	return wu_ok;
}

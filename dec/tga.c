#include <string.h>
#include <time.h>
#include <ctype.h>

#include "../wudefs.h"
#include "../common.h"

#include "lib/tga.h"

static void print_extension_area(const struct tga_metadata *meta,
struct wu_tree *tree) {
	tree_sprout_unsafe_leaf(tree, "Author name", meta->author.name,
		sizeof(meta->author.name));
	tree_sprout_unsafe_leaf(tree, "Author comment", meta->author.comment,
		sizeof(meta->author.comment));

	union wu_leaf val;
	if (meta->has_stamp) {
		val.time = utc_to_epoch(&meta->stamp);
		tree_bud_leaf(tree, "Timestamp", wu_leaf_time, val);
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
		val.u = meta->software.version_number;
		tree_bud_leaf(tree, "Software version number",
			wu_leaf_unsigned, val);
	}
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

	if (umax(desc.w, desc.h) > wuconf->max_img_size) {
		tga_cleanup(&desc);
		return wu_exceeded_size_limit;
	}

	tree_sprout_unsafe_leaf(&infile->metadata, "ID", desc.meta->id,
		desc.meta->id_len);
	if (tga_parse_footer(&desc)) {
		struct wu_tree *extra = tree_sprout_branch(&infile->metadata,
			"Extension area data");
		print_extension_area(desc.meta, extra);
		infile->bg[0] = desc.meta->key_color.r;
		infile->bg[1] = desc.meta->key_color.g;
		infile->bg[2] = desc.meta->key_color.b;
		infile->bg[3] = desc.meta->key_color.a;
	}

	const bool extra_palette = desc.map.entry
		&& desc.type != colormap_data && desc.type != colormap_rle;
	const bool stamp = desc.meta && desc.meta->stamp_offset;

	struct raw_img *img = alloc_sub_images(infile,
		1U + extra_palette + stamp);
	if (!img) {
		tga_cleanup(&desc);
		return wu_alloc_error;
	}

	unsigned char *palette = (unsigned char *)tga_take_palette(&desc);
	const bool needs_palette = palette && !extra_palette;

	img[0].data = tga_decode(&desc);
	if (!img[0].data) {
		tga_cleanup(&desc);
		free(palette);
		return wu_decoding_error;
	}
	img[0].w = desc.w;
	img[0].h = desc.h;
	img[0].channels = desc.ch;
	img[0].bitdepth = 8;
	if (desc.bitdepth == 8 && !needs_palette) {
		img[0].layout = gray;
	} else {
		img[0].layout = bgra;
	}
	img[0].mirror = !(desc.orientation & 0x02);
	img[0].rotate = (unsigned char)((desc.orientation & 0x01) * 2);

	int i = 0;
	if (extra_palette) {
		++i;
		img[i].data = palette;
		img[i].id = strdup("extra_palette");
		img[i].w = 16;
		img[i].h = 16;
		img[i].bitdepth = 8;
		img[i].channels = 4;
		img[i].layout = bgra;
	} else {
		img[0].palette = palette;
	}

	if (stamp) {
		++i;
		unsigned int swidth, sheight;
		img[i].data = tga_decode_stamp(&desc, &swidth, &sheight);
		if (!img[i].data) {
			realloc_sub_images(infile, infile->nr - 1);
		} else {
			if (img[0].palette) {
				img[i].palette = malloc(256 * 4);
				if (!img[i].palette) {
					realloc_sub_images(infile,
						infile->nr - 1);
					tga_cleanup(&desc);
					return wu_ok;
				}
				memcpy(img[i].palette, img[0].palette, 256 * 4);
			}

			img[i].id = strdup("stamp");
			img[i].w = swidth;
			img[i].h = sheight;
			img[i].channels = img[0].channels;
			img[i].bitdepth = img[0].bitdepth;
			img[i].layout = img[0].layout;
			img[i].mirror = img[0].mirror;
			img[i].rotate = img[0].rotate;
		}
	}

	tga_cleanup(&desc);
	return wu_ok;
}

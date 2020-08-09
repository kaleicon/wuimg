#include <string.h>
#include <ctype.h>

#include "wudefs.h"
#include "common.h"

#include "lib_tga.h"

static void print_extension_area(const struct tga_metadata *meta, FILE *out) {
	print_unsafe_data(meta->author.name, sizeof(meta->author.name),
		"Author name", true, out);

	print_unsafe_data(meta->author.comment, sizeof(meta->author.comment),
		"Author comment", true, out);

	if (meta->stamp.month || meta->stamp.day || meta->stamp.year
	|| meta->stamp.hour || meta->stamp.minute || meta->stamp.second) {
		fprintf(out, "Timestamp: %.4hu-%.2hu-%.2hu %.2hu:%.2hu:%.2hu\n",
			meta->stamp.year, meta->stamp.month, meta->stamp.day,
			meta->stamp.hour, meta->stamp.minute, meta->stamp.second);
	}

	print_unsafe_data(meta->job.name, sizeof(meta->job.name), "Job ID",
		true, out);

	if (meta->job.hour || meta->job.minute || meta->job.second) {
		fprintf(out, "Job time: %.2hu:%.2hu:%.2hu\n",
			meta->job.hour, meta->job.minute, meta->job.second);
	}

	print_unsafe_data(meta->software.id, sizeof(meta->software.id),
		"Software ID", true, out);

	if (isgraph(meta->software.version_letter)) {
		fprintf(out, "Software version letter: %c\n",
			meta->software.version_letter);
	}
	if (meta->software.version_number) {
		fprintf(out, "Software version number: %hu\n",
			meta->software.version_number);
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

	print_unsafe_data(desc.meta->id, desc.meta->id_len, NULL, true,
		infile->meta.fp);
	if (tga_parse_footer(&desc)) {
		print_extension_area(desc.meta, infile->meta.fp);
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

#include <stdlib.h>
#include <string.h>
#include <setjmp.h>

#include <png.h>

#include "../wudefs.h"
#include "../common.h"
#include "../metadata.h"

struct png_state {
	png_struct *png;
	png_info *info;
	png_info *end;
};

static void free_png_state(struct png_state *png) {
	png_destroy_read_struct(&png->png, &png->info, &png->end);
}

static void little_trouble_fn(png_struct *png, const char *msg) {
	struct image_file *infile = png_get_error_ptr(png);
	image_file_error_append(infile, msg);
}

static void big_trouble_fn(png_struct *png, const char *msg) {
	little_trouble_fn(png, msg);
	png_longjmp(png, 1);
}

static void read_png_info(const png_struct *png, png_info *info,
struct wu_tree *tree) {
	png_text *text = NULL;
	const int num_comm = png_get_text(png, info, &text, NULL);
	if (text) {
		struct wu_tree *branch = NULL;
		for (int i = 0; i < num_comm; ++i) {
			if (!strcmp(text[i].key, "XML:com.adobe.xmp")) {
				standard_metadata(xmp_metadata, text[i].text,
					strlen(text[i].text), tree);
			} else {
				if (!branch) {
					branch = tree_findadd_branch(tree, "Text");
				}
				if (branch) {
					tree_sprout_leaf(branch, text[i].key,
						text[i].text);
				}
			}
		}
	}

	png_time *time = NULL;
	png_get_tIME(png, info, &time);
	if (time) {
		const struct wu_leaf leaf = {
			.val.time = utc_to_epoch(time->year, time->month,
				time->day, time->hour, time->minute,
				time->second),
			.type = wu_leaf_time,
		};
		tree_bud_leaf(tree, "Time", leaf);
	}

	png_byte *exif = NULL;
	uint32_t len;
	png_get_eXIf_1(png, info, &len, &exif);
	if (exif) {
		standard_metadata(exif_metadata, exif, len, tree);
	}
}

static void read_png_metadata(const struct png_state *png,
struct image_file *infile) {
	png_info *infos[] = {png->info, png->end};
	for (size_t i = 0; i < ARRAY_LEN(infos); ++i) {
		png_color_16 *bg = NULL;
		png_get_bKGD(png->png, infos[i], &bg);
		if (bg) {
			infile->bg.r = (unsigned char)(bg->red >> 8);
			infile->bg.g = (unsigned char)(bg->green >> 8);
			infile->bg.b = (unsigned char)(bg->blue >> 8);
			infile->bg.a = 0xff;
			break;
		}
	}
	for (size_t i = 0; i < ARRAY_LEN(infos); ++i) {
		read_png_info(png->png, infos[i], &infile->metadata);
	}
}

static struct raster_pal * read_palette(const struct png_state *png) {
	struct raster_pal *palette = malloc(sizeof(*palette));
	if (palette) {
		png_color *plte;
		int plte_num;
		png_get_PLTE(png->png, png->info, &plte, &plte_num);

		png_byte *trns = NULL;
		int trns_num = 0;
		png_get_tRNS(png->png, png->info, &trns, &trns_num, NULL);

		for (int i = 0; i < plte_num; ++i) {
			palette->color[i].r = plte[i].red;
			palette->color[i].g = plte[i].green;
			palette->color[i].b = plte[i].blue;
			palette->color[i].a = (i < trns_num) ? trns[i] : 0xff;
		}
	}
	return palette;
}

static enum wu_error decode_image(struct image_file *infile,
const struct wu_conf *wuconf, struct png_state *png) {
//#ifdef PNG_APNG_SUPPORTED
//	infile->nr = png_get_num_frames(png->png, png->info);
//#else
	infile->nr = 1;
//#endif
	struct raw_img *img = alloc_sub_images(infile, infile->nr);
	if (!img) {
		return wu_alloc_error;
	}

	const png_byte bit_depth = png_get_bit_depth(png->png, png->info);
	const png_byte color_type = png_get_color_type(png->png, png->info);
	if (color_type == PNG_COLOR_TYPE_PALETTE) {
		if (!raw_img_set_palette(img, read_palette(png))) {
			png_set_expand(png->png);
		}
	} else if (bit_depth >= 16 && which_end() != big_endian) {
		png_set_swap(png->png);
	}
	int passes = 1;
#ifdef PNG_READ_INTERLACING_SUPPORTED
	passes = png_set_interlace_handling(png->png);
#endif
	png_read_update_info(png->png, png->info);

	img->w = png_get_image_width(png->png, png->info);
	img->h = png_get_image_height(png->png, png->info);
	img->channels = png_get_channels(png->png, png->info);
	img->bitdepth = png_get_bit_depth(png->png, png->info);
	if (zumax(img->w, img->h) > wuconf->max_img_size) {
		return wu_exceeds_size_limit;
	}

	const size_t row_size = raw_img_addbuf(img);
	if (!row_size) {
		return wu_alloc_error;
	}

	for (int p = 0; p < passes; ++p) {
		for (size_t y = 0; y < img->h; ++y) {
			png_read_row(png->png, img->data + row_size*y, NULL);
		}
	}
	return wu_ok;
}

static enum wu_error dec_wrap(struct image_file *infile,
const struct wu_conf *wuconf, struct png_state *png) {
	png->info = png_create_info_struct(png->png);
	if (!png->info) {
		return wu_alloc_error;
	}

	if (setjmp(png_jmpbuf(png->png))) {
		return wu_decoding_error;
	}

	// We've already checked the signature for this stream
	png_init_io(png->png, infile->ifp);
	png_set_user_limits(png->png, wuconf->max_img_size, wuconf->max_img_size);
	png_set_crc_action(png->png, PNG_CRC_WARN_USE, PNG_CRC_WARN_DISCARD);
	png_read_info(png->png, png->info);

	const enum wu_error err = decode_image(infile, wuconf, png);
	if (err != wu_ok) {
		return err;
	}

	png->end = png_create_info_struct(png->png);
	if (png->end) {
		png_read_end(png->png, png->end);
	}

	read_png_metadata(png, infile);
	return wu_ok;
}

enum wu_error png_dec(struct image_file *infile,
const struct wu_conf *wuconf) {
	struct png_state png = {0};
	png.png = png_create_read_struct(PNG_LIBPNG_VER_STRING,
		infile, big_trouble_fn, little_trouble_fn);
	if (png.png) {
		const enum wu_error err = dec_wrap(infile, wuconf, &png);
		free_png_state(&png);
		return err;
	}
	return wu_alloc_error;
}

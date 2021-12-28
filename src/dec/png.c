#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <setjmp.h>

#include <png.h>

#include "../wudefs.h"
#include "../common.h"
#include "../metadata.h"
#include "../raster/pal.h"

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
		struct utc_time utc = {
			.year = time->year,
			.mon = time->month,
			.day = time->day,
			.hour = time->hour,
			.min = time->minute,
			.sec = time->second,
		};
		const struct wu_leaf leaf = {
			.val.time = utc_to_epoch(&utc),
			.type = wu_leaf_time
		};
		tree_bud_leaf(tree, "Time", leaf);
	}

	png_byte *exif = NULL;
	uint32_t len; /* Unhelpfully called num_exif in the docs */
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

static unsigned char ** setup_png_output(struct image_file *infile,
struct png_state *png) {
//#ifdef PNG_APNG_SUPPORTED
//	infile->nr = png_get_num_frames(png->png, png->info);
//#else
	infile->nr = 1;
//#endif
	struct raw_img *img = alloc_sub_images(infile, infile->nr);
	if (!img) {
		return NULL;
	}

	const png_byte bit_depth = png_get_bit_depth(png->png, png->info);
	const png_byte color_type = png_get_color_type(png->png, png->info);
	if (color_type == PNG_COLOR_TYPE_PALETTE) {
		if (!raw_img_set_palette(img, read_palette(png))) {
			png_set_expand(png->png);
		}
	} else if (bit_depth >= 16) {
		png_set_swap(png->png);
	}
	png_set_interlace_handling(png->png);
	png_read_update_info(png->png, png->info);

	img->w = png_get_image_width(png->png, png->info);
	img->h = png_get_image_height(png->png, png->info);
	if (img->u.palette) {
		img->channels = 1;
	} else {
		img->channels = png_get_channels(png->png, png->info);
	}
	img->bitdepth = png_get_bit_depth(png->png, png->info);

	const size_t row_size = raw_img_addbuf(img);
	if (!row_size) {
		return NULL;
	}

	unsigned char **row_ptr = malloc(img->h * sizeof(*row_ptr));
	if (!row_ptr) {
		return NULL;
	}
	for (size_t i = 0; i < img->h; ++i) {
		row_ptr[i] = img->data + i*row_size;
	}
	return row_ptr;
}

enum wu_error png_dec(struct image_file *infile,
const struct wu_conf *wuconf) {
	struct png_state png = {0};
	png.png = png_create_read_struct(PNG_LIBPNG_VER_STRING,
		infile, big_trouble_fn, little_trouble_fn);
	if (!png.png) {
		return wu_alloc_error;
	}

	png.info = png_create_info_struct(png.png);
	if (!png.info) {
		free_png_state(&png);
		return wu_alloc_error;
	}

	if (setjmp(png_jmpbuf(png.png))) {
		free_png_state(&png);
		return wu_decoding_error;
	}

	// We've already checked the signature for this stream
	png_init_io(png.png, infile->ifp);
	png_set_crc_action(png.png, PNG_CRC_WARN_USE, PNG_CRC_WARN_DISCARD);

/*	png_byte apng_chunks[] = {
		'a', 'c', 'T', 'L', 0,
		'f', 'c', 'T', 'L', 0,
		'f', 'd', 'T', 'L', 0,
	};
	png_set_keep_unknown_chunks(png.png, 3, apng_chunks, 3);*/

	png_read_info(png.png, png.info);

	const unsigned int width = png_get_image_width(png.png, png.info);
	const unsigned int height = png_get_image_height(png.png, png.info);
	if (umax(width, height) > wuconf->max_img_size) {
		free_png_state(&png);
		return wu_exceeds_size_limit;
	}

//	png_unknown_chunk *unknowns;
//	int num_chunks = png_get_unknown_chunks(png.png, png.info, &unknowns);
//	check_chunks(unknowns, num_chunks);

	unsigned char **rows = setup_png_output(infile, &png);
	if (!rows) {
		free_png_state(&png);
		return wu_alloc_error;
	}
	png_read_image(png.png, rows);
	free(rows);

	png.end = png_create_info_struct(png.png);
	if (png.end) {
		png_read_end(png.png, png.end);
//		num_chunks = png_get_unknown_chunks(png.png, png.end, &unknowns);
//		check_chunks(unknowns, num_chunks);
	}

	read_png_metadata(&png, infile);
	free_png_state(&png);
	return wu_ok;
}

// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2021 kaleido
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>

#include <png.h>

#include "wudefs.h"
#include "misc/common.h"
#include "misc/endian.h"
#include "misc/metadata.h"
#include "misc/time.h"

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
	image_file_strerror_append(infile, msg);
}

static void big_trouble_fn(png_struct *png, const char *msg) {
	little_trouble_fn(png, msg);
	png_longjmp(png, 1);
}

static void read_png_info(const png_struct *png, png_info *info,
struct image_file *infile) {
	struct wutree *tree = &infile->metadata;
	struct wuimg *img = infile->sub_img;

#ifdef PNG_bKGD_SUPPORTED
	png_color_16 *bg = NULL;
	png_get_bKGD(png, info, &bg);
	if (bg) {
		infile->bg.r = (unsigned char)(bg->red >> 8);
		infile->bg.g = (unsigned char)(bg->green >> 8);
		infile->bg.b = (unsigned char)(bg->blue >> 8);
		infile->bg.a = 0xff;
	}
#endif /* bKGD */

#ifdef PNG_TEXT_SUPPORTED
	png_text *text = NULL;
	const int num_comm = png_get_text(png, info, &text, NULL);
	if (text) {
		struct wutree *branch = NULL;
		for (int i = 0; i < num_comm; ++i) {
			if (!strcmp(text[i].key, "XML:com.adobe.xmp")) {
				metadata_parse(metadata_xmp, text[i].text,
					strlen(text[i].text), tree);
			} else {
				if (!branch) {
					branch = tree_findadd_branch(tree, "Text");
				}
				if (branch) {
					tree_add_leaf(branch, text[i].key,
						text[i].text, NULL);
				}
			}
		}
	}
#endif /* TEXT */

#ifdef PNG_tIME_SUPPORTED
	png_time *time = NULL;
	png_get_tIME(png, info, &time);
	if (time) {
		const time_t t = utc_to_epoch(time->year, time->month,
			time->day, time->hour, time->minute, time->second);
		tree_bud_leaf_time(tree, "Time", t);
	}
#endif /* tIME */

#ifdef PNG_eXIf_SUPPORTED
	png_byte *exif = NULL;
	png_uint_32 len;
	png_get_eXIf_1(png, info, &len, &exif);
	if (exif && metadata_parse(metadata_exif, exif, len, tree)) {
		wuimg_exif_orientation(img, metadata_orientation(tree));
	}
#endif
}

static void read_png_metadata(const struct png_state *png,
struct image_file *infile) {
	png_info *infos[] = {png->info, png->end};
	for (size_t i = 0; i < ARRAY_LEN(infos); ++i) {
		read_png_info(png->png, infos[i], infile);
	}
}

static void get_color_profile(const png_struct *png, png_info *info,
struct color_space *cs) {
#ifdef PNG_iCCP_SUPPORTED
	if (cs->type != color_profile_icc) {
		char *name;
		unsigned char *icc = NULL;
		png_uint_32 len;
		png_get_iCCP(png, info, &name, NULL, &icc, &len);
		if (icc && color_space_set_icc_copy(cs, icc, len)) {
			return;
		}
	} else {
		return;
	}
#endif

	if (png_get_valid(png, info, PNG_INFO_sRGB)) {
		return;
	}

#ifdef PNG_cHRM_SUPPORTED
	double p[8];
	if (png_get_cHRM(png, info, p, p+1, p+2, p+3, p+4, p+5, p+6, p+7)) {
		color_space_set_primaries(cs, p[0], p[1], p[2], p[3],
			p[4], p[5], p[6], p[7]);
	}
#endif

#ifdef PNG_gAMA_SUPPORTED
	double gamma;
	if (png_get_gAMA(png, info, &gamma)) {
		color_space_set_gamma(cs, 1/gamma);
	}
#endif
	(void)png; (void)info; (void)cs;
}

static bool read_palette(const struct png_state *png, struct wuimg *img) {
	struct palette *palette = wuimg_palette_init(img);
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
	struct wuimg *img = infile->sub_img;
	img->w = png_get_image_width(png->png, png->info);
	img->h = png_get_image_height(png->png, png->info);
	img->channels = png_get_channels(png->png, png->info);
	img->bitdepth = png_get_bit_depth(png->png, png->info);
	img->alpha = alpha_unassociated;
	if (wuimg_exceeds_limit(img, wuconf)) {
		return wu_exceeds_size_limit;
	}
	if (png_get_color_type(png->png, png->info) == PNG_COLOR_TYPE_PALETTE) {
		if (!read_palette(png, img)) {
			return wu_alloc_error;
		}
	}
	img->ratio = png_get_pixel_aspect_ratio(png->png, png->info);

	const enum wu_error st = wuimg_alloc(img);
	if (st != wu_ok) {
		return st;
	}

	const size_t stride = wuimg_stride(img);
	int passes = 1;
#ifdef PNG_READ_INTERLACING_SUPPORTED
	passes = png_set_interlace_handling(png->png);
#endif

	/* We swap bytes ourselves as it's slightly faster for some reason.
	 * Perhaps not enabling any transforms at all speeds things up. */
	for (int p = 0; p < passes; ++p) {
		for (size_t y = 0; y < img->h; ++y) {
			void *row = img->data + stride*y;
			png_read_row(png->png, row, NULL);
			if (p == passes - 1 && img->bitdepth > 8) {
				endian_loop16(row, big_endian, stride/2);
			}
		}
	}
	get_color_profile(png->png, png->info, &img->cs);
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
	fseek(infile->ifp, 8, SEEK_CUR);
	png_init_io(png->png, infile->ifp);
	png_set_sig_bytes(png->png, 8);
#ifdef PNG_SET_USER_LIMITS_SUPPORTED
	png_set_user_limits(png->png, wuconf->max_img_size, wuconf->max_img_size);
#endif
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

static enum wu_error png_dec(struct image_file *infile,
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

const struct image_fn png_fn = {
	.alloc_single = true,
	.dec = png_dec
};

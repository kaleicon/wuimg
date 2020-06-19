#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <setjmp.h>

#include <png.h>

#include "wudefs.h"
#include "common.h"

static void check_chunks(const png_unknown_chunkp unknowns,
const int num_chunks) {
	for (int i = 0; i < num_chunks; ++i) {
		const png_byte *name = unknowns[i].name;
		printf("Chunk found: %s", name);
		switch (name[0]) {
		case 'a':
			printf(" frames: %u\n"
				" loops: %u\n",
				endian_u32(unknowns[i].data, big_endian),
				endian_u32(unknowns[i].data+4, big_endian));
			break;
		case 'f':
			printf(" seq_num: %u\n",
				endian_u32(unknowns[i].data, big_endian));
			break;
		}
	}
}

static void print_png_comment(const png_textp text_ptr) {
	print_unsafe_data(text_ptr->key, strlen(text_ptr->key), NULL, false);
	printf(": ");
	print_unsafe_data(text_ptr->text, strlen(text_ptr->text), NULL, true);
}

static unsigned char read_palette(png_structp png_ptr, png_infop info_ptr,
struct raw_img *img, const png_byte bit_depth) {
	const size_t pal_len = 1U << bit_depth;
	img->palette = malloc(pal_len * 4);
	if (!img->palette) {
		return 0;
	}

	png_colorp plte;
	int plte_num;
	png_get_PLTE(png_ptr, info_ptr, &plte, &plte_num);

	png_bytep trns = NULL;
	int trns_num;
	png_get_tRNS(png_ptr, info_ptr, &trns, &trns_num, NULL);

	for (int i = 0; i < plte_num; ++i) {
		img->palette[i*4] = plte[i].red;
		img->palette[i*4 + 1] = plte[i].green;
		img->palette[i*4 + 2] = plte[i].blue;
	}

	unsigned char channels;
	if (trns && trns_num) {
		channels = 4;
		int i = 0;
		for (; i < trns_num; ++i) {
			img->palette[i*4 + 3] = trns[i];
		}
		for (; i < plte_num; ++i) {
			img->palette[i*4 + 3] = 0xff;
		}
	} else {
		channels = 3;
	}

	return channels;
}

static unsigned char ** setup_png_output(struct image_file *infile,
png_structp png_ptr, png_infop info_ptr) {
//#ifdef PNG_APNG_SUPPORTED
//	infile->nr = png_get_num_frames(png_ptr, info_ptr);
//#else
	infile->nr = 1;
//#endif
	struct raw_img *img = alloc_sub_images(infile, infile->nr);
	if (!img) {
		return NULL;
	}

	const png_byte bit_depth = png_get_bit_depth(png_ptr, info_ptr);
	const png_byte color_type = png_get_color_type(png_ptr, info_ptr);
	unsigned char pal_channels = 0;
	if (color_type == PNG_COLOR_TYPE_PALETTE) {
		png_set_packing(png_ptr);
		pal_channels = read_palette(png_ptr, info_ptr, img, bit_depth);
		if (!pal_channels) {
			png_set_expand(png_ptr);
		}
	} else if (bit_depth <= 8) {
		png_set_expand(png_ptr);
	} else if (bit_depth >= 16) {
		png_set_swap(png_ptr);
	}
	png_set_interlace_handling(png_ptr);
	png_read_update_info(png_ptr, info_ptr);

	img->w = png_get_image_width(png_ptr, info_ptr);
	img->h = png_get_image_height(png_ptr, info_ptr);
	img->channels = png_get_channels(png_ptr, info_ptr);
	img->bitdepth = png_get_bit_depth(png_ptr, info_ptr);

	const size_t row_size = img->w * img->channels * img->bitdepth/8;
	const size_t buffer_size = row_size * img->h;
	img->data = malloc(buffer_size);
	if (!img->data) {
		return NULL;
	}

	unsigned char **row_ptr = malloc(img->h * sizeof(unsigned char *));
	if (!row_ptr) {
		return NULL;
	}

	for (size_t i = 0; i < img->h; ++i) {
		row_ptr[i] = &img->data[i*row_size];
	}

	if (img->palette) {
		img->channels = pal_channels;
	}
	return row_ptr;
}

enum wu_error png_dec(struct image_file *infile,
const struct wu_conf *wuconf) {
	png_structp png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING,
		NULL, NULL, NULL);
	if (!png_ptr) {
		return wu_alloc_error;
	}
	png_infop info_ptr = png_create_info_struct(png_ptr);
	if (!info_ptr) {
		png_destroy_read_struct(&png_ptr, NULL, NULL);
		return wu_alloc_error;
	}

	if (setjmp(png_jmpbuf(png_ptr))) {
		png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
		return wu_decoding_error;
	}

	// We've already checked the signature for this stream
	fseek(infile->ifp, 8, SEEK_SET);
	png_init_io(png_ptr, infile->ifp);
	png_set_sig_bytes(png_ptr, 8);
	png_set_crc_action(png_ptr, PNG_CRC_WARN_USE, PNG_CRC_WARN_DISCARD);

	png_byte apng_chunks[] = {
		'a', 'c', 'T', 'L', 0,
		'f', 'c', 'T', 'L', 0,
		'f', 'd', 'T', 'L', 0,
	};
	png_set_keep_unknown_chunks(png_ptr, 3, apng_chunks, 3);

	png_read_info(png_ptr, info_ptr);
	const unsigned int width = png_get_image_width(png_ptr, info_ptr);
	const unsigned int height = png_get_image_height(png_ptr, info_ptr);
	if (umax(width, height) > wuconf->max_img_size) {
		png_read_end(png_ptr, NULL);
		png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
		return wu_exceeded_size_limit;
	}

	png_textp text_ptr;
	const int num_comm = png_get_text(png_ptr, info_ptr, &text_ptr, NULL);
	for (int i = 0; i < num_comm; ++i) {
		print_png_comment(&text_ptr[i]);
	}

	png_unknown_chunkp unknowns;
	int num_chunks = png_get_unknown_chunks(png_ptr, info_ptr, &unknowns);
	check_chunks(unknowns, num_chunks);

	unsigned char **rows = setup_png_output(infile, png_ptr, info_ptr);
	if (!rows) {
		png_read_end(png_ptr, NULL);
		png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
		return wu_alloc_error;
	}
	png_read_image(png_ptr, rows);
	free(rows);

	png_infop end_ptr = png_create_info_struct(png_ptr);
	if (end_ptr) {
		png_read_end(png_ptr, end_ptr);
		num_chunks = png_get_unknown_chunks(png_ptr, end_ptr, &unknowns);
		check_chunks(unknowns, num_chunks);
	}

	png_color_16p background;
	if (png_get_bKGD(png_ptr, info_ptr, &background)) {
		infile->bg[0] = (unsigned char)(background->red >> 8);
		infile->bg[1] = (unsigned char)(background->green >> 8);
		infile->bg[2] = (unsigned char)(background->blue >> 8);
		infile->bg[3] = 0xff;
	}
	png_destroy_read_struct(&png_ptr, &info_ptr, &end_ptr);
	return wu_ok;
}

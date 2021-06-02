#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <setjmp.h>

#include <png.h>

#include "../wudefs.h"
#include "../common.h"
#include "../metadata.h"
#include "lib/common/unpack.h"

/*static void check_chunks(const png_unknown_chunkp unknowns,
const int num_chunks) {
	for (int i = 0; i < num_chunks; ++i) {
		const png_byte *name = unknowns[i].name;
		printf("Chunk found: %s", name);
		switch (name[0]) {
		case 'a':
			printf(" frames: %u\n"
				" loops: %u\n",
				endian32(unknowns[i].data, big_endian),
				endian32(unknowns[i].data+4, big_endian));
			break;
		case 'f':
			printf(" seq_num: %u\n",
				endian32(unknowns[i].data, big_endian));
			break;
		}
	}
}*/

static void print_png_comment(const png_textp ptr, struct wu_tree *tree) {
	const char *text = ptr->text ? ptr->text : "";
	tree_sprout_leaf(tree, ptr->key, text);
}

static unsigned char * read_palette(png_structp png_ptr, png_infop info_ptr) {
	struct colormap *palette = malloc(sizeof(*palette) * 256);
	if (palette) {
		png_colorp plte;
		int plte_num;
		png_get_PLTE(png_ptr, info_ptr, &plte, &plte_num);

		png_bytep trns = NULL;
		int trns_num;
		png_get_tRNS(png_ptr, info_ptr, &trns, &trns_num, NULL);

		for (int i = 0; i < plte_num; ++i) {
			palette[i].r = plte[i].red;
			palette[i].g = plte[i].green;
			palette[i].b = plte[i].blue;
			palette[i].a = 0xff;
		}

		if (trns && trns_num) {
			for (int i = 0; i < trns_num; ++i) {
				palette[i].a = trns[i];
			}
		}
	}

	return (unsigned char *)palette;
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
	if (color_type == PNG_COLOR_TYPE_PALETTE) {
		png_set_packing(png_ptr);
		img->palette = read_palette(png_ptr, info_ptr);
		if (!img->palette) {
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
	png_textp text_ptr;
	const int num_comm = png_get_text(png_ptr, info_ptr, &text_ptr, NULL);
	for (int i = 0; i < num_comm; ++i) {
		print_png_comment(&text_ptr[i], &infile->metadata);
	}

	const unsigned int width = png_get_image_width(png_ptr, info_ptr);
	const unsigned int height = png_get_image_height(png_ptr, info_ptr);
	if (umax(width, height) > wuconf->max_img_size) {
		png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
		return wu_exceeds_size_limit;
	}

//	png_unknown_chunkp unknowns;
//	int num_chunks = png_get_unknown_chunks(png_ptr, info_ptr, &unknowns);
//	check_chunks(unknowns, num_chunks);

	unsigned char **rows = setup_png_output(infile, png_ptr, info_ptr);
	if (!rows) {
		png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
		return wu_alloc_error;
	}
	png_read_image(png_ptr, rows);
	free(rows);

	png_infop end_ptr = png_create_info_struct(png_ptr);
	if (end_ptr) {
		png_read_end(png_ptr, end_ptr);
//		num_chunks = png_get_unknown_chunks(png_ptr, end_ptr, &unknowns);
//		check_chunks(unknowns, num_chunks);
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

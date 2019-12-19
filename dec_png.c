#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <setjmp.h>

#include <png.h>

#include "wudefs.h"

static unsigned char ** setup_png_output(struct image_file *infile,
png_structp png_ptr, png_infop info_ptr) {
	const png_byte bit_depth = png_get_bit_depth(png_ptr, info_ptr);
	const png_byte color_type = png_get_color_type(png_ptr, info_ptr);
	if (bit_depth < 8 || color_type == PNG_COLOR_TYPE_PALETTE) {
		png_set_expand(png_ptr);
	}
	png_set_interlace_handling(png_ptr);
	png_read_update_info(png_ptr, info_ptr);

//#ifdef PNG_APNG_SUPPORTED
//	infile->nr = png_get_num_frames(png_ptr, info_ptr);
//#else
	infile->nr = 1;
//#endif
	struct raw_img *restrict img = alloc_sub_images(infile, infile->nr);

	img->w = png_get_image_width(png_ptr, info_ptr);
	img->h = png_get_image_height(png_ptr, info_ptr);
	img->channels = png_get_channels(png_ptr, info_ptr);
	img->bitdepth = png_get_bit_depth(png_ptr, info_ptr);
	img->true_bitdepth = bit_depth;
	const size_t row_size = img->w * img->channels * img->bitdepth/8;
	const size_t buffer_size = row_size * img->h;
	img->data = malloc(buffer_size);
	unsigned char **row_ptr = malloc(img->h * sizeof(unsigned char *));
	for (size_t i = 0; i < img->h; ++i) {
		row_ptr[i] = &img->data[i*row_size];
	}
	return row_ptr;
}

enum wu_error_type png_dec(struct image_file *infile) {
	FILE *ifp = fopen(infile->name, "rb");
	if (!ifp) {
		infile->err_msg = strerror(errno);
		return wu_open_error;
	}
	const size_t sig_len = 8;
	unsigned char signature[sig_len];
	if (fread(signature, 1, sig_len, ifp) != sig_len) {
		infile->err_msg = strdup("Reached EOF when reading signature.");
		fclose(ifp);
		return wu_unexpected_eof;
	}
	if (png_sig_cmp(signature, 0, sig_len)) {
		infile->err_msg = strdup("File is not a valid PNG file.");
		fclose(ifp);
		return wu_invalid_sig;
	}

	png_structp png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING,
		NULL, NULL, NULL);
	if (!png_ptr) {
		fclose(ifp);
		return wu_alloc_error;
	}
	png_infop info_ptr = png_create_info_struct(png_ptr);
	if (!info_ptr) {
		fclose(ifp);
		png_destroy_read_struct(&png_ptr, NULL, NULL);
		return wu_alloc_error;
	}

	if (setjmp(png_jmpbuf(png_ptr))) {
		png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
		fclose(ifp);
		return wu_decoding_error;
	}

	png_init_io(png_ptr, ifp);
	png_set_sig_bytes(png_ptr, (int)sig_len);
	png_set_crc_action(png_ptr, PNG_CRC_WARN_USE, PNG_CRC_WARN_USE);

/*	png_byte apng_chunks[] = {
		'a', 'c', 'T', 'L', 0,
		'f', 'c', 'T', 'L', 0,
		'f', 'd', 'T', 'L', 0,
	};
	png_set_keep_unknown_chunks(png_ptr, 2, apng_chunks, 3);
*/
	png_read_info(png_ptr, info_ptr);

	unsigned char **rows = setup_png_output(infile, png_ptr, info_ptr);
	png_read_image(png_ptr, rows);

	png_read_end(png_ptr, NULL);
	png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
	fclose(ifp);
	return 0;
}

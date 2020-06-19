#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <flif.h>

#include "wudefs.h"
#include "common.h"

enum wu_error flif_dec(struct image_file *infile,
const struct wu_conf *wuconf) {
	size_t size;
	unsigned char *data = read_file_to_mem(infile->ifp, &size);
	if (!data) {
		return wu_alloc_error;
	}

	FLIF_DECODER *dec = flif_create_decoder();
	const int32_t success = flif_decoder_decode_memory(dec, data, size);
	if (!success) {
		infile->err_msg = strdup(
			"flif_decoder_decode_file() returned 0");
		flif_destroy_decoder(dec);
		free(data);
		return wu_decoding_error;
	}

	struct raw_img *img = alloc_sub_images(infile,
		flif_decoder_num_images(dec));
	if (!img) {
		flif_destroy_decoder(dec);
		free(data);
		return wu_alloc_error;
	}
	infile->is_animation = (infile->nr > 1);

	for (size_t i = 0; i < infile->nr; ++i) {
		FLIF_IMAGE *frame = flif_decoder_get_image(dec, i);

		img[i].w = flif_image_get_width(frame);
		img[i].h = flif_image_get_height(frame);
		if (zumax(img[i].w, img[i].h) > wuconf->max_img_size) {
			flif_destroy_decoder(dec);
			free(data);
			return wu_exceeded_size_limit;
		}
		img[i].channels = flif_image_get_nb_channels(frame);
		img[i].bitdepth = flif_image_get_depth(frame);
		void (*read_func)(FLIF_IMAGE*, uint32_t, void*, size_t);
		if (img[i].channels == 1) {
			read_func = flif_image_read_row_GRAY8;
		} else {
			// There are no RGB functions.
			if (img[i].channels == 3) {
				img[i].true_channels = 3;
				img[i].channels = 4;
			}
			if (img[i].bitdepth == 8) {
				read_func = flif_image_read_row_RGBA8;
			} else {
				read_func = flif_image_read_row_RGBA16;
			}
		}
		const size_t row_size = img[i].w * img[i].channels
			* img[i].bitdepth / 8;
		img[i].data = malloc(row_size * img[i].h);
		for (uint32_t row = 0; row < img[i].h; ++row) {
			unsigned char *pos = &(img[i].data[row * row_size]);
			read_func(frame, row, pos, row_size);
		}
	}

	flif_destroy_decoder(dec);
	free(data);
	return 0;
}

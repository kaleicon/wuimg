#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <flif.h>

#include "wudefs.h"
#include "common.h"

enum wu_error_type flif_dec(struct image_file *infile) {
	FLIF_DECODER *flif_dec = flif_create_decoder();
	const int32_t success = flif_decoder_decode_file(flif_dec, infile->name);
	if (!success) { // 0 means failure
		infile->err_msg = strdup(
			"flif_decoder_decode_file() returned 0 on flif_dec()"
		);
		return wu_decoding_error;
	}

	struct raw_img *img = alloc_sub_images(infile,
		flif_decoder_num_images(flif_dec));

	for (size_t i = 0; i < infile->nr; ++i) {
		FLIF_IMAGE *frame = flif_decoder_get_image(flif_dec, i);

		if (infile->nr > 1) {
			img[i].id = id_template("frame", i);
		}

		img[i].w = flif_image_get_width(frame);
		img[i].h = flif_image_get_height(frame);
		img[i].channels = flif_image_get_nb_channels(frame);
		img[i].bitdepth = flif_image_get_depth(frame);
		void (*read_func)(FLIF_IMAGE*, uint32_t, void*, size_t);
		if (img[i].channels == 1) {
			read_func = flif_image_read_row_GRAY8;
		} else {
			// For some reason there are no RGB functions.
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

	flif_destroy_decoder(flif_dec);
	return 0;
}

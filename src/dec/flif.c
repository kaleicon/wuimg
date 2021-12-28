#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <flif.h>

#include "../wudefs.h"
#include "../common.h"

enum wu_error flif_dec(struct image_file *infile,
const struct wu_conf *wuconf) {
	struct mmap_info map;
	if (!mmap_file(&map, infile->ifp)) {
		return wu_alloc_error;
	}

	FLIF_DECODER *dec = flif_create_decoder();
	const int32_t success = flif_decoder_decode_memory(dec, map.data, map.len);
	if (!success) {
		image_file_error_append(infile,
			"Decoder failed to read from memory");
		flif_destroy_decoder(dec);
		munmap_file(map);
		return wu_decoding_error;
	}

	if (!alloc_sub_images(infile, flif_decoder_num_images(dec))) {
		flif_destroy_decoder(dec);
		munmap_file(map);
		return wu_alloc_error;
	}
	infile->is_animation = (infile->nr > 1);

	enum wu_error status = wu_ok;
	size_t o = 0;
	for (size_t i = 0; i < infile->nr; ++i) {
		struct raw_img *img = infile->sub_img + o;
		FLIF_IMAGE *frame = flif_decoder_get_image(dec, i);

		img->w = flif_image_get_width(frame);
		img->h = flif_image_get_height(frame);
		if (zumax(img->w, img->h) > wuconf->max_img_size) {
			continue;
		}
		img->channels = flif_image_get_nb_channels(frame);
		img->bitdepth = flif_image_get_depth(frame);
		img->msec = (int)flif_image_get_frame_delay(frame);

		void (*read_func)(FLIF_IMAGE *, uint32_t, void *, size_t);
		if (img->channels == 1 && flif_image_get_palette_size(frame)) {
			raw_img_set_palette(img, malloc(sizeof(*img->u.palette)));
		}

		if (img->u.palette) {
			flif_image_get_palette(frame, img->u.palette);
			read_func = flif_image_read_row_PALETTE8;
		} else {
			if (img->channels == 1) {
				read_func = flif_image_read_row_GRAY8;
			} else {
				// There are no RGB functions.
				if (img->channels == 3) {
					img->channels = 4;
					img->disable_alpha = true;
				}
				if (img->bitdepth == 8) {
					read_func = flif_image_read_row_RGBA8;
				} else {
					read_func = flif_image_read_row_RGBA16;
				}
			}
		}
		const size_t row_size = raw_img_addbuf(img + o);
		if (!row_size) {
			status = wu_alloc_error;
			break;
		}

		for (uint32_t row = 0; row < img->h; ++row) {
			unsigned char *pos = img->data + row * row_size;
			read_func(frame, row, pos, row_size);
		}
		++o;
	}

	flif_destroy_decoder(dec);
	munmap_file(map);
	if (status > wu_ok) {
		return status;
	}
	return image_file_total_decoded(infile, o);
}

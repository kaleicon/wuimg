// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2025 kaleido
#include <stdlib.h>
#include "misc/bit.h"
#include "misc/file.h"
#include "wudefs.h"

static struct wu_st init_icc(struct image_file *infile,
const struct wu_conf *conf) {
	size_t size = file_remaining(infile->ifp);
	uint8_t *cpy = malloc(size);
	if (!cpy) {
		return WUERR_HERE(wu_alloc_error);
	}
	size = fread(cpy, 1, size, infile->ifp);
	if (!size) {
		return WUERR_HERE(wu_unexpected_eof);
	}
	struct wuimg *img = infile->sub_img;
	const uint8_t bits = 8;
	const uint8_t blue_bits = 4;
	img->w = 1 << bits << blue_bits/2;
	img->h = img->w;
	img->channels = 4;
	img->bitdepth = 8;
	img->bitrange = bits;
	img->layout = pix_rgba;
	if (!color_space_set_icc_owned(&img->cs, cpy, size)) {
		return WUERR_HERE(wu_alloc_error);
	}
	enum wu_error e = wuimg_alloc_limit(img, conf);
	if (e != wu_ok) {
		return WUERR_HERE(e);
	}
	const uint32_t mask = bit_set32(bits);
	for (size_t y = 0; y < img->h; ++y) {
		for (size_t x = 0; x < img->w; ++x) {
			size_t i = y*img->w + x;
			img->data[i*4] = (uint8_t)(x & mask);
			img->data[i*4+1] = (uint8_t)(y & mask);
			img->data[i*4+2] = (uint8_t)(
				(y >> bits << blue_bits/2 | x >> bits)*0x11
			);
			img->data[i*4+3] = (uint8_t)mask;
		}
	}
	return WU_OK;
}

const struct image_fn icc_fn = {
	.alloc_single = true,
	.init = init_icc,
};

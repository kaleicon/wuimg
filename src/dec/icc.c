// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2025 kaleido
#include <stdlib.h>
#include "misc/bit.h"
#include "misc/file.h"
#include "wudefs.h"

static const uint8_t ICC_CARD_BITS = 8;
static const uint8_t ICC_CARD_BLUE_BITS = 4;

static struct wu_st event_icc(struct image_file *infile, struct wu_state *state,
const enum image_event ev) {
	(void)state;
	if (ev == ev_subcycle) {
		struct wuimg *img = infile->sub_img;
		const uint32_t mask = bit_set32(ICC_CARD_BITS);
		for (size_t y = 0; y < img->h; ++y) {
			for (size_t x = 0; x < img->w; ++x) {
				size_t i = y*img->w + x;
				img->data[i*4] = (uint8_t)(x & mask);
				img->data[i*4+1] = (uint8_t)(y & mask);
				img->data[i*4+2] = (uint8_t)(0x11u * (
					y >> ICC_CARD_BITS << ICC_CARD_BLUE_BITS/2
						| x >> ICC_CARD_BITS
				));
				img->data[i*4+3] = (uint8_t)mask;
			}
		}
		return WU_OK;
	}
	return WU_NO_CHANGE;
}

static struct wu_st init_icc(struct image_file *infile) {
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
	img->w = 1 << ICC_CARD_BITS << ICC_CARD_BLUE_BITS/2;
	img->h = img->w;
	img->channels = 4;
	img->bitdepth = 8;
	img->bitrange = ICC_CARD_BITS;
	img->layout = pix_rgba;
	return color_space_set_icc_owned(&img->cs, cpy, size)
		? WU_OK
		: WUERR_HERE(wu_alloc_error);
}

const struct image_fn icc_fn = {
	.alloc_single = true,
	.alloc_on_subcycle = true,
	.init = init_icc,
	.event = event_icc,
};

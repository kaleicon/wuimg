// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2021 kaleido
#include "misc/endian.h"
#include "avs.h"

// I like this format.

enum wu_error avs_open_file(struct wuimg *img, FILE *ifp) {
	uint32_t buf[2];
	if (fread(buf, sizeof(buf), 1, ifp)) {
		img->w = endian32(buf[0], big_endian);
		img->h = endian32(buf[1], big_endian);
		img->channels = 4;
		img->bitdepth = 8;
		img->layout = pix_argb;
		return wu_ok;
	}
	return wu_unexpected_eof;
}

// SPDX-License-Identifier: 0BSD
#include "raster/fmt.h"
#include "farbfeld.h"

enum wu_error farbfeld_open_file(struct wuimg *img, FILE *ifp) {
	const uint8_t magic[8] = "farbfeld";
	uint32_t buf[4];
	if (fread(buf, sizeof(buf), 1, ifp)) {
		if (!memcmp(magic, buf, sizeof(magic))) {
			img->w = endian32(buf[2], big_endian);
			img->h = endian32(buf[3], big_endian);
			img->channels = 4;
			img->bitdepth = 16;
			return wu_ok;
		}
		return wu_invalid_signature;
	}
	return wu_unexpected_eof;
}

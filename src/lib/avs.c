#include <stdio.h>
#include <stdlib.h>

#include "../common.h"

#include "avs.h"

// I like this format.

enum lib_fail avs_open_file(struct raster_desc *desc, FILE *ifp) {
	uint32_t buf[2];
	if (fread(buf, sizeof(buf), 1, ifp)) {
		*desc = (struct raster_desc) {
			.w = endian32(buf[0], big_endian),
			.h = endian32(buf[1], big_endian),
			.ch = 4,
			.bitdepth = 8,
			.layout = pix_argb,
		};
		if (desc->w && desc->h) {
			raster_normalize(desc);
			return lib_ok;
		}
		return lib_invalid_header;
	}
	return lib_unexpected_eof;
}

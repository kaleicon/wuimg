#include <stdio.h>
#include <stdlib.h>

#include "../common.h"
#include "../raster/lib.h"
#include "wbmp.h"

static size_t read_uintvar_dim(FILE *ifp) {
	size_t value = 0;
	for (size_t i = 0; i < sizeof(value); ++i) {
		const int c = getc(ifp);
		if (c == EOF) {
			break;
		}
		value = (value << 7) | ((unsigned int)c & 0x7f);
		if (c >> 7 == 0) {
			return value;
		}
	}
	return 0;
}

enum lib_fail wbmp_open_file(struct raster_desc *desc, FILE *ifp) {
	unsigned char buf[2];
	if (fread(buf, 1, sizeof(buf), ifp)) {
		if (!memchk(buf, 0, sizeof(buf))) {
			*desc = (struct raster_desc) {
				.w = read_uintvar_dim(ifp),
				.h = read_uintvar_dim(ifp),
				.ch = 1,
				.bitdepth = 1,
			};
			if (desc->w && desc->h) {
				raster_normalize(desc);
				return lib_ok;
			}
		}
		return lib_invalid_header;
	}
	return lib_unexpected_eof;
}

#include <stdio.h>
#include <stdlib.h>

#include "../common.h"
#include "../raster/lib.h"
#include "wbmp.h"

static enum lib_fail read_uintvar_dim(FILE *ifp, size_t *value) {
	for (size_t i = 7; i < sizeof(*value) * 8; i += 7) {
		const int c = getc(ifp);
		if (c == EOF) {
			return lib_unexpected_eof;
		}
		*value = (*value << 7) | ((unsigned)c & 0x7f);
		if (c >> 7 == 0) {
			return lib_ok;
		}
	}
	return lib_int_overflow;
}

enum lib_fail wbmp_open_file(struct raster_desc *desc, FILE *ifp) {
	unsigned char sig[2] = {0};
	enum lib_fail status = lib_sigcmp(sig, sizeof(sig), ifp);
	if (status == lib_ok) {
		*desc = (struct raster_desc) {
			.ch = 1,
			.bitdepth = 1,
		};

		status = read_uintvar_dim(ifp, &desc->w);
		if (status != lib_ok) {
			return status;
		}
		status = read_uintvar_dim(ifp, &desc->h);
		if (status != lib_ok) {
			return status;
		}
		if (!desc->w || !desc->h) {
			return lib_invalid_header;
		}
		raster_normalize(desc);
	}
	return status;
}

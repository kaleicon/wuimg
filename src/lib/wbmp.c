// SPDX-License-Identifier: 0BSD
#include <stdlib.h>

#include "raster/fmt.h"
#include "wbmp.h"

static enum wu_error read_uintvar_dim(FILE *ifp, size_t *value) {
	for (size_t i = 7; i < sizeof(*value) * 8; i += 7) {
		const int c = getc(ifp);
		if (c == EOF) {
			return wu_unexpected_eof;
		}
		*value = (*value << 7) | ((unsigned)c & 0x7f);
		if (c >> 7 == 0) {
			return wu_ok;
		}
	}
	return wu_int_overflow;
}

enum wu_error wbmp_open_file(struct wuimg *img, FILE *ifp) {
	unsigned char sig[2] = {0};
	enum wu_error status = fmt_sigcmp(sig, sizeof(sig), ifp);
	if (status == wu_ok) {
		img->channels = 1;
		img->bitdepth = 1;

		status = read_uintvar_dim(ifp, &img->w);
		if (status == wu_ok) {
			status = read_uintvar_dim(ifp, &img->h);
		}
	}
	return status;
}

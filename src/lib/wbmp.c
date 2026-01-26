// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2021 kaleido
#include <string.h>

#include "wbmp.h"

static struct wu_st read_uintvar_dim(FILE *ifp, size_t *value) {
	for (size_t i = 7; i < sizeof(*value) * 8; i += 7) {
		const int c = getc(ifp);
		if (c == EOF) {
			return WUERR_HERE(wu_unexpected_eof);
		}
		*value = (*value << 7) | ((unsigned)c & 0x7f);
		if (c >> 7 == 0) {
			return WU_OK;
		}
	}
	return wuerr(wu_int_overflow, "uintvar too long");
}

struct wu_st wbmp_open_file(struct wuimg *img, FILE *ifp) {
	const unsigned char sig[2] = {0};
	unsigned char buf[2];
	if (!fread(buf, sizeof(buf), 1, ifp)) {
		return WUERR_HERE(wu_alloc_error);
	} else if (memcmp(buf, sig, sizeof(sig))) {
		return WUERR_HERE(wu_invalid_signature);
	}

	img->channels = 1;
	img->bitdepth = 1;
	struct wu_st status = read_uintvar_dim(ifp, &img->w);
	if (wu_isok(status)) {
		status = read_uintvar_dim(ifp, &img->h);
	}
	return status;
}

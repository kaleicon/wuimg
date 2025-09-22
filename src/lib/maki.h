// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2022 kaleido
#ifndef LIB_MAKI
#define LIB_MAKI

#include "raster/wuimg.h"

enum maki_version {
	maki_1a = 'A',
	maki_1b = 'B',
};

struct maki_desc {
	FILE *ifp;
	uint8_t model[4];
	char comment[20];
	enum maki_version version:8;
	uint16_t x, y;
};

const char * maki_version_str(enum maki_version version);

struct wu_st maki_decode(const struct maki_desc *desc, struct wuimg *img);

struct wu_st maki_parse(struct maki_desc *desc, struct wuimg *img, FILE *ifp);

#endif /* LIB_MAKI */

// SPDX-License-Identifier: 0BSD
#ifndef LIB_PX
#define LIB_PX

#include "raster/wuimg.h"

enum px_type {
	px_type_01 = 0x01,
	px_type_04 = 0x04,
	px_type_07 = 0x07,
	px_type_0c = 0x0c,
	px_type_90 = 0x90,
	px_type_40 = 0x40,
	px_type_44 = 0x44,
};

struct px_tile {
	uint32_t len;
	uint16_t w, h;
	long data_start;
};

struct px_desc {
	FILE *ifp;
	uint32_t nr;
	uint16_t w, h;
	enum px_type type:16;
	struct px_tile tile;
};

enum wu_error px_decode(const struct px_desc *desc, struct wuimg *img,
uint32_t idx);

enum wu_error px_parse(struct px_desc *desc, FILE *ifp);

#endif /* LIB_PX */

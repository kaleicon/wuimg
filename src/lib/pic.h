// SPDX-License-Identifier: 0BSD
#ifndef LIB_PIC
#define LIB_PIC

#include <stdio.h>

#include "raster/wuimg.h"

enum pic_type {
	pic_type_x68k = 0x0,
	pic_type_pc_88va = 0x1,
	pic_type_fm_towns = 0x2,
	pic_type_mac = 0x3,
	pic_type_generic = 0xf,
};

struct pic_bits {
	uint8_t s; // shared bit
	uint8_t grb[3]; // color bitdepth
	uint16_t mul[3]; // scale value
};

struct pic_desc {
	FILE *ifp;
	struct wustr comm;
	int16_t x, y;
	struct pic_bits bits;
	uint8_t depth;
	enum pic_type type:8;
	uint8_t mode;
	bool tiled;
};

const char * pic_model_str(enum pic_type type);

void pic_cleanup(struct pic_desc *desc);

size_t pic_decode(const struct pic_desc *desc, struct wuimg *img);

enum wu_error pic_parse(struct pic_desc *desc, struct wuimg *img);

enum wu_error pic_open(struct pic_desc *desc, FILE *ifp);

#endif /* LIB_PIC */

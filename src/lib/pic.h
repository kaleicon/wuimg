// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2022 kaleido
#ifndef LIB_PIC
#define LIB_PIC

#include "misc/mparser.h"
#include "raster/wuimg.h"

enum pic_type {
	pic_type_x68k = 0x0,
	pic_type_pc_88va = 0x1,
	pic_type_fm_towns = 0x2,
	pic_type_mac = 0x3,
	pic_type_generic = 0xf,
};

struct pic_bits {
	uint8_t shared;
	uint8_t uni;
	uint8_t and;
	uint16_t mul;
};

struct pic_desc {
	struct wuptr data;
	struct wuptr comm;
	struct wuptr dummy;
	int16_t x, y;
	struct pic_bits bits;
	uint8_t depth;
	enum pic_type type:8;
	uint8_t mode;
	bool tiled;
};

const char * pic_model_str(enum pic_type type);

struct wu_st pic_decode(const struct pic_desc *desc, struct wuimg *img);

struct wu_st pic_parse(struct pic_desc *desc, struct wuimg *img,
struct wuptr mem);

#endif /* LIB_PIC */

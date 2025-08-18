// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2025 kaleido
#ifndef LIB_EA
#define LIB_EA

#include "raster/wuimg.h"

struct eafnt_desc {
	FILE *ifp;
	uint16_t chars;
	uint8_t image_code;
	bool reverse;
	uint32_t char_off, unk_off, img_off;
};

struct wu_st eafnt_load(struct eafnt_desc *desc, struct wuimg *img);

struct wu_st eafnt_init(struct eafnt_desc *desc, struct wuimg *img, FILE *ifp);

#endif /* LIB_EA */

// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2024 kaleido
#ifndef LIB_CBG
#define LIB_CBG

#include "raster/wuimg.h"
#include "misc/mparser.h"

enum cbg_version {
	cbg_v1 = 1,
	cbg_v2 = 2,
};

struct cbg_desc {
	struct mparser mp;
	uint32_t weights_len;
	uint32_t key;
	uint8_t sum, xor;
	enum cbg_version version:8;
};

struct wu_st cbg_decode(const struct cbg_desc *desc, struct wuimg *img);

struct wu_st cbg_parse(struct cbg_desc *desc, struct wuimg *img,
struct wuptr mem);

#endif /* LIB_CBG */

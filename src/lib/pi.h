// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2021 kaleido
#ifndef LIB_PI
#define LIB_PI

#include "raster/wuimg.h"

struct pi_saver {
	unsigned char model[4];
	struct wuptr data;
};

struct pi_desc {
	unsigned char depth;

	struct wuptr comm;
	struct wuptr dummy;
	struct pi_saver saver;
	struct wuptr data;
};

struct wu_st pi_decode(const struct pi_desc *desc, struct wuimg *img);

struct wu_st pi_read_header(struct pi_desc *desc, struct wuimg *img,
struct wuptr mem);

#endif /* LIB_PI */

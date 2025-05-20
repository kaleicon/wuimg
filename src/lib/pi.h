// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2021 kaleido
#ifndef LIB_PI
#define LIB_PI

#include "misc/mparser.h"
#include "raster/wuimg.h"

struct pi_saver {
	unsigned char model[4];
	struct wuptr data;
};

struct pi_desc {
	struct mparser mp;
	unsigned char depth;

	struct wuptr comm;
	struct wuptr dummy;
	struct pi_saver saver;
};

size_t pi_decode(const struct pi_desc *desc, struct wuimg *img);

enum wu_error pi_read_header(struct pi_desc *desc, struct wuimg *img);

enum wu_error pi_init(struct pi_desc *desc, struct wuptr mem);

#endif /* LIB_PI */

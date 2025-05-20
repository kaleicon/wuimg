// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2025 kaleido
#ifndef LIB_XBM
#define LIB_XBM

#include <stdbool.h>

#include "raster/wuimg.h"
#include "misc/wustr.h"
#include "misc/mparser.h"

enum xbm_type {
	xbm_x10,
	xbm_x11,
};

enum c_fmt {
	c_xbm,
	c_degas_icon,
};

struct xbm_info {
	enum xbm_type version;
	bool has_hotspot;
	intmax_t x_hot, y_hot;
};

struct degas_icon_info {
	size_t size;
};

struct c_desc {
	struct mparser tp;
	enum c_fmt fmt;
	struct xbm_info xbm;
};

struct wu_st c_decode(const struct c_desc *desc, struct wuimg *img);

struct wu_st c_parse(struct c_desc *desc, struct wuimg *img, struct wuptr mem);

#endif /* LIB_XBM */

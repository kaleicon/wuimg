// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2025 kaleido
#ifndef LIB_MGX
#define LIB_MGX

#include "raster/wuimg.h"

enum mgxicn_color {
	mgxicn_color_rgba8 = 0,
	mgxicn_color_gray8 = 1,
};

struct mgxicn_desc {
	FILE *ifp;
	enum mgxicn_color color;
	uint32_t nr;
	uint32_t idx;
};

struct wu_st mgxicn_load(struct mgxicn_desc *desc, struct wuimg *img);

struct wu_st mgxicn_set_next(struct mgxicn_desc *desc, struct wuimg *img);

struct wu_st mgxicn_init(struct mgxicn_desc *desc, FILE *ifp);

#endif /* LIB_MGX */

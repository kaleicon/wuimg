// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2026 kaleido
#ifndef LIB_MAYAICON
#define LIB_MAYAICON

#include "raster/wuimg.h"

struct mayaicon_desc {
	FILE *ifp;
	uint32_t nr;
	uint32_t idx;
};

struct wu_st mayaicon_load(struct mayaicon_desc *desc, struct wuimg *img);

struct wu_st mayaicon_set_next(struct mayaicon_desc *desc, struct wuimg *img);

struct wu_st mayaicon_init(struct mayaicon_desc *desc, FILE *ifp);

#endif /* LIB_MAYAICON */

// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2022 kaleido
#ifndef LIB_PRT
#define LIB_PRT

#include "raster/wuimg.h"

enum prt_version {
	prt_v101 = 101,
	prt_v102 = 102,
};

struct prt_desc {
	FILE *ifp;
	struct palette *pal;
	enum prt_version version:8;
	uint8_t depth;
	bool mask;
	uint32_t x, y;
};

void prt_cleanup(struct prt_desc *desc);

struct wu_st prt_decode(const struct prt_desc *desc, struct wuimg *img);

struct wu_st prt_parse(struct prt_desc *desc, struct wuimg *img, FILE *ifp);

#endif /* LIB_PTR */

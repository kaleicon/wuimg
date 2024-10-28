// SPDX-License-Identifier: 0BSD
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

size_t prt_decode(const struct prt_desc *desc, struct wuimg *img);

enum wu_error prt_parse(struct prt_desc *desc, struct wuimg *img);

enum wu_error prt_open(struct prt_desc *desc, FILE *ifp);

#endif /* LIB_PTR */

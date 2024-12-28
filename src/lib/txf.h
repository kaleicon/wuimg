// SPDX-License-Identifier: 0BSD
#ifndef LIB_TXF
#define LIB_TXF

#include "raster/wuimg.h"

struct txf_desc {
	FILE *ifp;
	uint32_t max_ascent, max_descent;
};

size_t txf_load(const struct txf_desc *desc, struct wuimg *img);

enum wu_error txf_parse(struct txf_desc *desc, struct wuimg *img);

enum wu_error txf_init(struct txf_desc *desc, FILE *ifp);

#endif /* LIB_TXF */

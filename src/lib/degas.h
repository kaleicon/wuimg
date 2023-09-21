// SPDX-License-Identifier: 0BSD
#ifndef LIB_DEGAS
#define LIB_DEGAS

#include "raster/wuimg.h"

enum degas_res {
	degas_res_low = 0,
	degas_res_medium = 1,
	degas_res_high = 2,
};

struct degas_desc {
	FILE *ifp;
	size_t size;
	enum degas_res res:8;
	bool compressed;
};

const char * degas_res_str(const enum degas_res res);

size_t degas_decode(const struct degas_desc *desc, struct wuimg *img);

enum wu_error degas_parse(struct degas_desc *desc, struct wuimg *img);

enum wu_error degas_open(struct degas_desc *desc, FILE *ifp);

#endif /* LIB_DEGAS */

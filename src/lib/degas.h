// SPDX-License-Identifier: 0BSD
#ifndef LIB_DEGAS
#define LIB_DEGAS

#include "raster/wuimg.h"

enum degas_res {
	degas_res_low = 0,
	degas_res_medium = 1,
	degas_res_high = 2,
};

struct degas_crng {
	uint8_t lo;
	uint8_t cnt;
	bool reverse;
	bool active;
	float secs;
};

struct degas_desc {
	FILE *ifp;
	size_t size;
	enum degas_res res:8;
	bool compressed;
	bool is_elite;
	struct palette *crng_pal;
	struct degas_crng crng[4];
};

const char * degas_res_str(const enum degas_res res);

void degas_free(struct degas_desc *desc);

void degas_color_cycle(const struct degas_desc *desc, struct wuimg *img,
double time);

size_t degas_decode(struct degas_desc *desc, struct wuimg *img);

enum wu_error degas_parse(struct degas_desc *desc, struct wuimg *img);

enum wu_error degas_init(struct degas_desc *desc, FILE *ifp);

#endif /* LIB_DEGAS */

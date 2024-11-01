// SPDX-License-Identifier: 0BSD
#ifndef LIB_DEGAS
#define LIB_DEGAS

#include "raster/wuimg.h"

enum atarist_res {
	atarist_res_low = 0,
	atarist_res_medium = 1,
	atarist_res_high = 2,
};

const char * atarist_res_str(enum atarist_res res);


struct degas_desc {
	FILE *ifp;
	size_t size;
	enum atarist_res res:8;
	bool compressed;
	bool is_elite;
	struct palette_cycle *cycle;
};

void degas_free(struct degas_desc *desc);

size_t degas_decode(struct degas_desc *desc, struct wuimg *img);

enum wu_error degas_parse(struct degas_desc *desc, struct wuimg *img,
FILE *ifp);


struct tiny_desc {
	struct mparser mp;
	enum atarist_res res:8;
	uint16_t ctrl;
	uint16_t data;
	uint16_t iters;
	struct palette_cycle *cycle;
};

void tiny_free(struct tiny_desc *desc);

size_t tiny_decode(const struct tiny_desc *desc, struct wuimg *img);

enum wu_error tiny_parse(struct tiny_desc *desc, struct wuimg *img,
struct map_info map);

#endif /* LIB_DEGAS */

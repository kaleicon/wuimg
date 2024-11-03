// SPDX-License-Identifier: 0BSD
#ifndef LIB_DEGAS
#define LIB_DEGAS

#include "raster/wuimg.h"

enum atari_st_res {
	atari_st_res_low = 0,
	atari_st_res_medium = 1,
	atari_st_res_high = 2,
};

const char * atari_st_res_str(enum atari_st_res res);

/* Dali */
struct dali_desc {
	FILE *ifp;
	enum atari_st_res res;
};

size_t dali_decode(struct dali_desc *desc, struct wuimg *img);

enum wu_error dali_parse(struct dali_desc *desc, struct wuimg *img, FILE *ifp,
const uint8_t ext[static 3]);

/* DEGAS */
struct degas_desc {
	FILE *ifp;
	size_t size;
	enum atari_st_res res:8;
	bool compressed;
	bool is_elite;
	struct palette_cycle *cycle;
};

void degas_free(struct degas_desc *desc);

size_t degas_decode(struct degas_desc *desc, struct wuimg *img);

enum wu_error degas_parse(struct degas_desc *desc, struct wuimg *img,
FILE *ifp);

/* MegaPaint */
struct bld_desc {
	FILE *ifp;
	bool compressed;
};

size_t bld_decode(struct bld_desc *desc, struct wuimg *img);

enum wu_error bld_parse(struct bld_desc *desc, struct wuimg *img, FILE *ifp);

/* Tiny Stuff */
struct tiny_desc {
	struct mparser mp;
	enum atari_st_res res:8;
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

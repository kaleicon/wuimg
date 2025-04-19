// SPDX-License-Identifier: 0BSD
#ifndef LIB_GXA
#define LIB_GXA

#include "raster/wuimg.h"

enum gxa_compression {
	gxa_none = 0,
	gxa_rle = 1,
	gxa_mystery2 = 2,
};

struct gxa_desc {
	FILE *ifp;
	struct palette *pal;
	enum gxa_compression compression:16;
	uint16_t nb_images;
	uint32_t len;
	uint8_t comment[32];
	uint8_t comment_len;
};

const char * gxa_compression_str(enum gxa_compression c);

void gxa_cleanup(struct gxa_desc *desc);

struct wu_st gxa_load_image(struct gxa_desc *desc, struct wuimg *img);

struct wu_st gxa_next_image(struct gxa_desc *desc, struct wuimg *img);

struct wu_st gxa_init(struct gxa_desc *desc, FILE *ifp);


enum bsi_compression {
	bsi_none = 0,
	bsi_scanlines = 4,
};

struct bsi_desc {
	FILE *ifp;
	struct palette *pal;
	uint16_t w, h;
	uint16_t nb_images;
	enum bsi_compression compression:16;
	bool bsif;
	uint32_t comp_len;
	long pos;
	uint32_t *table;
};

const char * bsi_compression_str(enum bsi_compression c);

void bsi_cleanup(struct bsi_desc *desc);

struct wu_st bsi_load_image(struct bsi_desc *desc, struct wuimg *img, uint16_t i);

struct wu_st bsi_set_image(struct bsi_desc *desc, struct wuimg *img);

struct wu_st bsi_init(struct bsi_desc *desc, FILE *ifp);

#endif /* LIB_GXA */

// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2026 kaleido
#ifndef LIB_CDI
#define LIB_CDI

#include "raster/wuimg.h"

enum cdi_model {
	cdi_rgb888 = 1,
	cdi_rgb555 = 2,
	cdi_dyuv = 3,
	cdi_clut8 = 4,
	cdi_clut7 = 5,
	cdi_clut4 = 6,
	cdi_clut3 = 7,
	cdi_rl7 = 8,
	cdi_rl3 = 9,
	cdi_plte = 10,
};

enum cdi_dyuv_start {
	cdi_dyuv_one = 0,
	cdi_dyuv_each = 1,
};

union cdi_yuvs {
	uint8_t one[3];
	uint8_t *each;
};

struct cdi_xy_s16 {
	int16_t x, y;
};

struct cdi_xy_u16 {
	uint16_t x, y;
};

struct cdi_ipar {
	struct cdi_xy_s16 off;
	struct cdi_xy_u16 src, hotspot;
	struct pix_rgb8 trans, mask;
};

struct cdi_desc {
	FILE *ifp;
	struct wuimg *img;
	uint32_t data_len;
	enum cdi_model model:8;
	enum cdi_dyuv_start dyuv_start:8;
	bool needs_plte;
	bool has_user_data;
	uint32_t user_len;
	struct cdi_ipar ipar;
	union cdi_yuvs yuvs;
};

const char * cdi_dyuv_start_str(enum cdi_dyuv_start type);

const char * cdi_model_str(enum cdi_model model);

void cdi_cleanup(struct cdi_desc *desc);

struct wu_st cdi_load(struct cdi_desc *desc);

struct wu_st cdi_init(struct cdi_desc *desc, struct wuimg *img, FILE *ifp);

#endif /* LIB_CDI */

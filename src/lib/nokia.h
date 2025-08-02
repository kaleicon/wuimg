// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2025 kaleido
#ifndef LIB_NOKIA
#define LIB_NOKIA

#include "raster/wuimg.h"

enum nlm_logo_type {
	nlm_operator = 0,
	nlm_caller = 1,
	nlm_startup = 2,
	nlm_picture = 3,
};

struct nlm_desc {
	FILE *ifp;
	enum nlm_logo_type logo_type;
	uint16_t nr_images;
	uint8_t w, h;
};

const char * nlm_logo_type_str(enum nlm_logo_type logo);

struct wu_st nlm_load(const struct nlm_desc *desc, struct wuimg *img,
uint8_t i);

struct wu_st nlm_image_info(const struct nlm_desc *desc, struct wuimg *img);

struct wu_st nlm_parse(struct nlm_desc *desc, FILE *ifp);


struct nol_desc {
	FILE *ifp;
	bool is_nol;
	uint16_t country, network;
	uint16_t mystery;
};

struct wu_st nol_load(const struct nol_desc *desc, struct wuimg *img);

struct wu_st nol_parse(struct nol_desc *desc, struct wuimg *img, FILE *ifp);


struct npm_desc {
	FILE *ifp;
	uint8_t mystery;
	uint8_t len;
	uint8_t comment[255];
};

struct wuptr npm_get_comment(const struct npm_desc *desc);

struct wu_st npm_load(const struct npm_desc *desc, struct wuimg *img);

struct wu_st npm_parse(struct npm_desc *desc, struct wuimg *img, FILE *ifp);


struct nsl_desc {
	FILE *ifp;
	struct wustr vers, modl;
};

void nsl_clean(struct nsl_desc *desc);

struct wu_st nsl_load(struct nsl_desc *desc, struct wuimg *img);

struct wu_st nsl_parse(struct nsl_desc *desc, struct wuimg *img, FILE *ifp);

#endif /* LIB_NOKIA */

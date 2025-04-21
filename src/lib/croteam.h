// SPDX-License-Identifier: 0BSD
#ifndef LIB_TBN
#define LIB_TBN

#include "raster/wuimg.h"

#define TBN_ANIMADAT_LEN 32

struct tbn_desc {
	FILE *ifp;
	uint32_t flags;
	uint32_t xres, yres;
	uint32_t unknown;
	uint32_t shr;
	size_t anim_off;
};

size_t tbn_read_animadat(struct tbn_desc *desc,
char data[static TBN_ANIMADAT_LEN]);

struct wu_st tbn_frame(struct tbn_desc *desc, struct wuimg *img, uint32_t i);

struct wu_st tbn_init(struct tbn_desc *desc, struct wuimg *img, FILE *ifp);

#endif /* LIB_TBN */

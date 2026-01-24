// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2021 kaleido
#ifndef LIB_PICTOR
#define LIB_PICTOR

#include "raster/wuimg.h"

enum pictor_palette_type {
	pictor_no_palette = 0,
	pictor_cga_palette = 1,
	pictor_pcjr_palette = 2,
	pictor_ega_palette = 3,
	pictor_vga_palette = 4,
	pictor_vga_too_i_think = 5,
};

struct pictor_desc {
	FILE *ifp;
	uint16_t x, y;
	uint8_t depth;
	uint8_t planes;
	char video_mode;
	enum pictor_palette_type pal_type:8;
	bool interleave;
	uint16_t blocks;
};

const char * pictor_palette_str(enum pictor_palette_type type);

const char * pictor_video_mode(const struct pictor_desc *desc);

struct wu_st pictor_decode(const struct pictor_desc *desc, struct wuimg *img);

struct wu_st pictor_parse(struct pictor_desc *desc, struct wuimg *img,
FILE *ifp);

#endif /* LIB_PICTOR */

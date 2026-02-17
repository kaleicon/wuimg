// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2022 kaleido
#ifndef LIB_MSX
#define LIB_MSX

#include <stdio.h>

#include "raster/wuimg.h"

enum msx_screen {
	msx_screen2 = '2',
	msx_screen3 = '3',
	msx_screen4 = '4',
	msx_screen5 = '5',
	msx_screen6 = '6',
	msx_screen7 = '7',
	msx_screen8 = '8',
	msx_screen10 = 'a',
	msx_screen12 = 'c',
};

enum msx_mod {
	msx_mod_alt_field = '1',
	msx_mod_graph_saurus = 'r',
};

struct msx_desc {
	FILE *ifp;
	enum msx_screen mode:8;
	enum msx_mod mod:8;
	bool compressed;
	bool external_palette;
	uint16_t end;
};

bool msx_mode_may_be_compressed(enum msx_screen mode);

bool msx_mode_may_have_alternate_field(enum msx_screen mode);

struct wu_st msx_decode(const struct msx_desc *desc, struct wuimg *img);

struct wu_st msx_parse(struct msx_desc *desc, struct wuimg *img, FILE *ifp,
const char *name, const uint8_t ext[static 3]);


struct wu_st msxgl_parse(struct wuimg *img, FILE *ifp, const char *name,
const uint8_t ext[static 4]);

#endif /* LIB_MSX */

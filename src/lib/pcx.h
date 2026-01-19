// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2020 kaleido
#ifndef LIB_PCX
#define LIB_PCX

#include <stdbool.h>

#include "raster/wuimg.h"
#include "misc/mparser.h"

enum pcx_version {
	pcx_ver25 = 0,
	pcx_ver28_egapal = 2,
	pcx_ver28_nopal = 3,
	pcx_paintbrush = 4,
	pcx_ver30 = 5,
};

struct pcx_desc {
	struct mparser mp;

	enum pcx_version version:8;
	bool compressed;
	bool palette_type;

	uint16_t xstart, ystart;
	uint16_t xend, yend;
	uint16_t horz_res, vert_res;
	uint16_t horz_screen, vert_screen;

	unsigned entries;
	const unsigned char *file_pal;
};

const char * pcx_version_string(enum pcx_version ver);

struct wu_st pcx_decode(struct pcx_desc *desc, struct wuimg *img);

struct wu_st pcx_read_header(struct pcx_desc *desc, struct wuimg *img,
struct wuptr mem, bool word_for_dos_variant);


struct dcx_desc {
	struct mparser mp;
	struct wuptr data;
	size_t nr;
};

struct wu_st dcx_set_file(const struct dcx_desc *dcx, struct pcx_desc *pcx,
struct wuimg *img, uint32_t i);

struct wu_st dcx_open_file(struct dcx_desc *desc, struct wuptr mem);


struct wu_st spidygfx_decode(const struct wuptr data, struct wuimg *img);

struct wu_st spidygfx_parse(struct wuptr *data, struct wuimg *img,
const struct wuptr mem);

#endif /* LIB_PCX */

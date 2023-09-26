// SPDX-License-Identifier: 0BSD
#ifndef LIB_PCX
#define LIB_PCX

#include <stdio.h>
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
	size_t rle_len;

	enum pcx_version version:8;
	bool palette_type;

	uint16_t bytes_per_line;
	uint16_t horz_res, vert_res;
	uint16_t horz_screen, vert_screen;

	unsigned entries;
	const unsigned char *file_pal;
};

const char * pcx_version_string(enum pcx_version ver);

enum wu_error pcx_decode(struct pcx_desc *desc, struct wuimg *img);

enum wu_error pcx_read_header(struct pcx_desc *desc, struct wuimg *img);

enum wu_error pcx_open_file(struct pcx_desc *desc, struct mparser mp);


struct dcx_desc {
	struct mparser mp;
	size_t nr;
	uint32_t *off;
};

void dcx_free(struct dcx_desc *desc);

enum wu_error dcx_set_file(const struct dcx_desc *dcx, struct pcx_desc *pcx,
uint32_t i);

enum wu_error dcx_open_file(struct dcx_desc *desc, struct mparser mp);

#endif /* LIB_PCX */

// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2020 kaleido
#ifndef LIB_SGI
#define LIB_SGI
#include <stdio.h>

#include "raster/wuimg.h"

enum sgi_bitmap_type {
	sgi_raw,
	sgi_332,
	sgi_colormap,
	sgi_colormap_define,
};

enum sgi_compression {
	sgi_uncompressed = 0,
	sgi_rle = 1,
};

struct sgi_desc {
	FILE *ifp;

	size_t rle_size;
	unsigned char bytedepth;
	enum sgi_compression compression:8;

	enum sgi_bitmap_type type:8;
	char name[80];
};

const char * sgi_compression_str(enum sgi_compression c);

struct wu_st sgi_decode(const struct sgi_desc *desc, struct wuimg *img);

struct wu_st sgi_parse_header(struct sgi_desc *desc, struct wuimg *img,
FILE *ifp);

#endif /* LIB_SGI */

#ifndef LIB_SGI
#define LIB_SGI

#include <stdio.h>
#include <stdbool.h>

#include "../raster/lib.h"

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
	struct raster_desc rast;

	size_t rle_size;
	unsigned char bytedepth;
	enum sgi_compression compression:8;

	enum sgi_bitmap_type type:8;
	char name[80];
};

unsigned char * sgi_decode(const struct sgi_desc *desc);

enum lib_fail sgi_parse_header(struct sgi_desc *desc);

enum lib_fail sgi_open_file(struct sgi_desc *desc, FILE *ifp);

#endif /* LIB_SGI */

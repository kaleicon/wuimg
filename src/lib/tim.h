#ifndef LIB_TIM
#define LIB_TIM

#include <stdio.h>

#include "../raster/lib.h"
#include "../raster/pal.h"

struct tim_clut {
	struct raster_pal *data;
	size_t nb;
	unsigned x, y;
};

struct tim_desc {
	FILE *ifp;
	unsigned x, y, w, h;
	unsigned line_len;
	unsigned char bitdepth;
	struct tim_clut clut;
};

void tim_cleanup(struct tim_desc *desc);

unsigned char * tim_decode(const struct tim_desc *desc);

struct raster_pal * tim_take_colormap(struct tim_desc *desc);

enum lib_fail tim_parse_header(struct tim_desc *desc);

enum lib_fail tim_open_file(FILE *ifp, struct tim_desc *desc);

#endif /* LIB_TIM */

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
	struct raster_desc r;
	unsigned x, y;
	struct tim_clut clut;
};

void tim_cleanup(struct tim_desc *desc);

size_t tim_decode(const struct tim_desc *desc, void *restrict dst);

enum lib_fail tim_parse_header(struct tim_desc *desc);

enum lib_fail tim_open_file(struct tim_desc *desc, FILE *ifp);

#endif /* LIB_TIM */

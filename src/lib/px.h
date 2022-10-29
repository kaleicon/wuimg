#ifndef LIB_PX
#define LIB_PX

#include "raster/wuimg.h"

enum px_type {
	px_type_0c = 0x0c,
};

struct px_tile {
	uint32_t len;
	uint16_t w, h;
	long data_start;
};

struct px_desc {
	FILE *ifp;
	uint32_t nr;
	uint16_t w, h;
	enum px_type type:16;
	struct px_tile tile;
};

enum wu_error px_decode(const struct px_desc *desc, struct wuimg *img,
uint32_t idx);

enum wu_error px_parse(struct px_desc *desc, FILE *ifp);

#endif /* LIB_PX */

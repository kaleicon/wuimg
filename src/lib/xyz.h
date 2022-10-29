#ifndef LIB_XYZ
#define LIB_XYZ

#include "raster/wuimg.h"
#include "misc/memparser.h"

struct xyz_desc {
	struct mp_parser mp;
	uint8_t *data;
};

void xyz_free(struct xyz_desc *desc, struct wuimg *img);

bool xyz_decode(struct xyz_desc *desc, struct wuimg *img);

enum wu_error xyz_parse(struct xyz_desc *desc, struct wuimg *img);

enum wu_error xyz_open(struct xyz_desc *desc, struct mp_parser mp);

#endif /* LIB_XYZ */

#ifndef LIB_XYZ
#define LIB_XYZ

#include "wudefs.h"
#include "raster/memparser.h"

struct xyz_desc {
	struct mp_parser mp;
	uint8_t *data;
};

void xyz_free(struct xyz_desc *desc, struct raw_img *img);

bool xyz_decode(struct xyz_desc *desc, struct raw_img *img);

enum wu_error xyz_parse(struct xyz_desc *desc, struct raw_img *img);

enum wu_error xyz_open(struct xyz_desc *desc, const struct map_info *mm);

#endif /* LIB_XYZ */

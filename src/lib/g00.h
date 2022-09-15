#ifndef LIB_G00
#define LIB_G00

#include "wudefs.h"

struct g00_block {
	uint16_t x, y;
	uint16_t info;
	uint16_t w, h;
	uint8_t *raster;
};

struct g00_part {
	uint16_t type;
	uint16_t block_count;
	uint32_t hotspot_x, hotspot_y;
	uint32_t width, height;
	uint32_t screen_x, screen_y;
	uint32_t full_part_w, full_part_h;
	struct g00_block *block;
};

struct g00_dir {
	uint32_t xstart, ystart;
	uint32_t xend, yend;
	uint32_t _reserved[2];
};

struct g00_desc_v1 {
	uint16_t pal_entries;
};

struct g00_desc_v2 {
	uint32_t dir_count;
	struct g00_dir *dir;
	struct g00_part *part;
};

enum g00_version {
	g00_v0 = 0,
	g00_v1 = 1,
	g00_v2 = 2,
};

struct g00_desc {
	FILE *ifp;
	enum g00_version version;
	uint32_t comp_size;
	size_t decomp_size;

	uint8_t *buf;

	union {
		struct g00_desc_v1 v1;
		struct g00_desc_v2 v2;
	} u;
};

void g00_cleanup(struct g00_desc *desc, struct raw_img *img);

size_t g00_decode(struct g00_desc *desc, struct raw_img *img);

enum wu_error g00_read_header(struct g00_desc *desc, struct raw_img *img,
FILE *ifp);

#endif /* LIB_G00 */

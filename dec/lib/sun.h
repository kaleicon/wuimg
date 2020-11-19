#ifndef LIB_SUN
#define LIB_SUN

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#include "common/lib.h"
#include "common/unpack.h"

enum sun_colormap_type {
	sun_no_colormap = 0,
	sun_rgb_colormap = 1,
	sun_raw_colormap = 2,
};

enum sun_type {
	sun_old = 0,
	sun_standard = 1,
	sun_byte_encoded = 2,
	sun_rgb = 3,
	sun_tiff = 4,
	sun_iff = 5,
	sun_experimental = 0xffff,
};

struct sun_desc {
	FILE *ifp;

	size_t data_len;
	uint32_t w, h;

	unsigned char bitdepth;
	unsigned char ch;
	enum sun_type type:16;
	uint32_t scan_len;

	struct sun_colormap {
		struct colormap *map;
		uint32_t len;
	} colormap;
};

void sun_cleanup(struct sun_desc *desc);

unsigned int sun_get_row_alignment(struct sun_desc *desc);

struct colormap * sun_take_colormap(struct sun_desc *desc);

unsigned char * sun_decode(const struct sun_desc *desc);

enum lib_fail sun_parse_header(struct sun_desc *desc);

enum lib_fail sun_open_file(FILE *ifp, struct sun_desc *desc);

#endif /* LIB_SUN */

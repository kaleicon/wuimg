// SPDX-License-Identifier: 0BSD
#ifndef LIB_SUN
#define LIB_SUN

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#include "raster/wuimg.h"

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
	enum sun_type type;
	enum sun_colormap_type colormap_type;
};

size_t sun_decode(const struct sun_desc *desc, struct wuimg *img);

enum wu_error sun_parse_header(struct sun_desc *desc, struct wuimg *img);

enum wu_error sun_open_file(struct sun_desc *desc, FILE *ifp);

#endif /* LIB_SUN */

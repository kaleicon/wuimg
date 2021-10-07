#ifndef COMMON_LIB
#define COMMON_LIB

#include <stdio.h>

#include "raster.h"

extern const char RASTER_EOF[];
extern const char RASTER_INV[];

enum lib_fail {
	lib_ok = 0,
	lib_unexpected_eof,
	lib_invalid_signature,
	lib_invalid_header,
	lib_unknown_format,
	lib_unsupported_format,
	lib_alloc_error,
	lib_invalid_data,
	lib_int_overflow,

	lib_pi_comment_too_long,

	lib_sgi_is_colormap_file,

	lib_sun_unsupported_type,
	lib_sun_experimental_type,
	lib_sun_uses_raw_colormap,

	lib_tga_no_image_data,

	lib_tim_mixed_bitdepth,
};

enum lib_pal {
	lib_pal_rgb,
	lib_pal_rgbx,
};

const char * lib_fail_string(enum lib_fail fail);

struct raster_pal * lib_raster_take_palette(struct raster_desc *desc);

void * lib_load_rast(FILE *ifp, const struct raster_desc *desc);

enum lib_fail lib_load_pal(FILE *ifp, struct raster_pal **pal,
enum lib_pal pal_type, size_t entries);

#endif /* COMMON_LIB */

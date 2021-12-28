#ifndef COMMON_LIB
#define COMMON_LIB

#include <stdio.h>

#include "../common.h"
#include "raster.h"

extern const char RASTER_EOF[];
extern const char RASTER_INV[];

enum lib_fail {
	lib_ok = 0,
	lib_unexpected_eof,
	lib_invalid_signature,
	lib_invalid_header,
	lib_unknown_format,
	lib_unsupported_feature,
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

void lib_raster_endian(void *data, const struct raster_desc *desc,
const enum endianness end);

size_t lib_load_rast(struct memory *mem, const struct raster_desc *desc,
FILE *ifp);

enum lib_fail lib_load_pal(FILE *ifp, struct raster_pal **pal,
enum lib_pal pal_type, size_t entries);

enum lib_fail lib_sigcmp(const unsigned char *restrict sig, size_t size,
FILE *ifp);

#endif /* COMMON_LIB */

#ifndef COMMON_LIB
#define COMMON_LIB

#include "../../../common.h"

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

	lib_pi_comment_too_long,

	lib_sgi_is_colormap_file,

	lib_sun_unsupported_type,
	lib_sun_experimental_type,
	lib_sun_uses_raw_colormap,

	lib_tga_no_image_data,

	lib_tim_mixed_bitdepth,
};
/*
enum lib_raster_format {
	lib_fmt_custom,
	lib_fmt_regular,
};

struct lib_raster_desc {
	FILE *ifp;
	unsigned char *data;

	size_t w, h;
	unsigned char planes;
	unsigned char bpc;
	unsigned char align;
	enum lib_raster_format fmt:8;
	enum endianness endian:8;

	struct {
		size_t len;
		struct colormap *map;
	} colormap;
};

void lib_cleanup(struct lib_raster_desc *desc);

struct colormap * lib_take_colormap(struct lib_raster_desc *desc);
*/
const char * lib_fail_string(enum lib_fail fail);

#endif /* COMMON_LIB */

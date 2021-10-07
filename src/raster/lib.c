#include <stdlib.h>

#include "../common.h"
#include "lib.h"

const char RASTER_EOF[] = "Warning: Got unexpected End Of File while reading "
	"data. Output may contain garbage.";
const char RASTER_INV[] = "Error: Invalid data found while decoding.";

const char * lib_fail_string(const enum lib_fail fail) {
	switch (fail) {
	case lib_ok:
		return "All OK";
	case lib_unexpected_eof:
		return "Unexpected EOF";
	case lib_invalid_signature:
		return "Invalid signature";
	case lib_invalid_header:
		return "Invalid format header";
	case lib_unknown_format:
		return "Unknown format variant";
	case lib_unsupported_format:
		return "Unsupported format variant";
	case lib_alloc_error:
		return "Memory allocation error";
	case lib_invalid_data:
		return "Invalid data in raster";
	case lib_int_overflow:
		return "Integer overflow";
	case lib_pi_comment_too_long:
		return "Pi error: Excessively long comment";
	case lib_sgi_is_colormap_file:
		return "SGI error: File defines or requires an external colormap";
	case lib_sun_unsupported_type:
		return "SUN error: Unsupported IFF or TIFF variant";
	case lib_sun_experimental_type:
		return "SUN error: File is marked Experimental";
	case lib_sun_uses_raw_colormap:
		return "SUN error: File uses an unknown 'raw' colormap";
	case lib_tga_no_image_data:
		return "TGA error: File is header only, and lacks image data";
	case lib_tim_mixed_bitdepth:
		return "TIM error: Unsupported mixed bitdepth variant";
	}
	return "???";
}

struct raster_pal * lib_raster_take_palette(struct raster_desc *desc) {
	struct raster_pal *pal = desc->palette;
	desc->palette = NULL;
	return pal;
}

void * lib_load_rast(FILE *ifp, const struct raster_desc *desc) {
	return fread_alloc(ifp, raster_stride(desc), desc->h);
}

enum lib_fail lib_load_pal(FILE *ifp, struct raster_pal **palette,
const enum lib_pal pal_type, const size_t entries) {
	*palette = malloc(sizeof(**palette));
	if (*palette) {
		struct raster_pal *pal = *palette;

		unsigned char *buf = (unsigned char *)pal;
		size_t elen = 4;
		if (pal_type == lib_pal_rgb) {
			elen = 3;
			buf += entries;
		}

		const size_t read = fread(buf, elen, entries, ifp);
		for (size_t i = 0; i < read; ++i) {
			switch (pal_type) {
			case lib_pal_rgb:
				pal->color[i].r = buf[i*elen];
				pal->color[i].g = buf[i*elen + 1];
				pal->color[i].b = buf[i*elen + 2];
				// fallthrough
			case lib_pal_rgbx:
				pal->color[i].a = 0xff;
			}
		}
		return read == entries ? lib_ok : lib_unexpected_eof;
	}
	return lib_alloc_error;
}

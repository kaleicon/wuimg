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

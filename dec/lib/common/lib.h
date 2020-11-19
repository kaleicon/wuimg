#ifndef COMMON_LIB
#define COMMON_LIB

//#define RASTER_EOF "Warning: Unexpected End Of File. Output may contain garbage."
//#define RASTER_INV "Error: Invalid data found while decoding."
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
};

const char * lib_fail_string(enum lib_fail fail);

#endif /* COMMON_LIB */

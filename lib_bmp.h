#ifndef LIB_BMP
#define LIB_BMP

#include <stdio.h>

#include "common_unpack.h"

enum bmp_fail {
	bmp_ok = 0,
	bmp_open_error,
	bmp_unexpected_eof,
	bmp_invalid_signature,
	bmp_invalid_header,
	bmp_unsupported_format,
	bmp_alloc_error,
};

enum bmp_rle_marker {
	end_of_scan_line = 0,
	end_of_rle,
	delta,
};

enum bmp_compression {
	bmp_no_compression = 0,
	bmp_8bit_rle,
	bmp_4bit_rle,
	bmp_mask,
};

enum bmp_order {
	bmp_bottom_up,
	bmp_top_down,
};

enum bmp_type {
	bmp_type2 = 12,
	bmp_type3 = 40,
	bmp_type4 = 108,
	bmp_type5 = 124,
};

struct bmp_mask {
	u_int32_t a, r, g, b;
};

struct bmp_desc {
	FILE *ifp;

	u_int32_t w, h;
	unsigned char bitdepth;
	enum bmp_type type:8;
	enum bmp_order order:8;
	enum bmp_compression compression:8;

	size_t scan_len;
	size_t data_len;

	union {
		struct colormap *pal;
		struct bmp_mask mask;
	};
};

void bmp_cleanup(const struct bmp_desc *desc);

unsigned int bmp_get_row_alignment(const struct bmp_desc *desc);

unsigned char * bmp_decode(const struct bmp_desc *desc);

struct colormap * bmp_take_colormap(struct bmp_desc *desc);

enum bmp_fail bmp_parse_header(struct bmp_desc *desc);

enum bmp_fail bmp_open_file(FILE *ifp, struct bmp_desc *desc);

#endif /* LIB_BMP */

#ifndef LIB_BMP
#define LIB_BMP

#include <stdio.h>
#include <stdlib.h>

#include "common/lib.h"
#include "common/unpack.h"

enum bmp_rle_marker {
	bmp_end_of_scan_line = 0,
	bmp_end_of_rle,
	bmp_delta,
};

enum bmp_compression {
	bmp_no_compression = 0,
	bmp_8bit_rle,
	bmp_4bit_rle,
	bmp_bitfield,
};

enum bmp_order {
	bmp_bottom_up,
	bmp_top_down,
};

enum bmp_type {
	bmp_type2 = 12,
	bmp_info_header = 40,
	bmp_v2_info_header = 52,
	bmp_v3_info_header = 56,
	bmp_v4_header = 108,
	bmp_v5_header = 124,
};

struct bmp_bitparams {
	uint32_t mask;
	uint32_t scale;
	uint32_t shift;
};

struct bmp_bitfield {
	struct bmp_bitparams p[4];
	bool high_depth;
};

struct bmp_desc {
	FILE *ifp;

	uint32_t w, h;
	unsigned char bitdepth;
	enum bmp_type type:8;
	enum bmp_order order:8;
	enum bmp_compression compression:8;

	size_t scan_len;
	size_t data_len;

	union {
		struct colormap *pal;
		struct bmp_bitfield bf;
	} bmp;
	bool expand;
};

void bmp_cleanup(const struct bmp_desc *desc);

unsigned int bmp_get_row_alignment(const struct bmp_desc *desc);

bool bmp_has_alpha(const struct bmp_desc *desc);

unsigned char * bmp_decode(const struct bmp_desc *desc);

struct colormap * bmp_take_colormap(struct bmp_desc *desc);

enum lib_fail bmp_parse_header(struct bmp_desc *desc);

enum lib_fail bmp_open_file(FILE *ifp, struct bmp_desc *desc);

#endif /* LIB_BMP */

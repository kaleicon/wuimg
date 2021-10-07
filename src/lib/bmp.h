#ifndef LIB_BMP
#define LIB_BMP

#include <stdio.h>
#include <stdlib.h>

#include "../raster/lib.h"
#include "../raster/pal.h"

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

struct bmp_bitfield {
	uint32_t shift;
	uint32_t mask;
	uint32_t scale;
};

struct bmp_desc {
	FILE *ifp;

	struct raster_desc r;
	unsigned char depth;
	enum bmp_type type:8;
	enum bmp_order order:8;
	enum bmp_compression compression:8;

	size_t data_len;

	struct bmp_bitfield bf[4];
};

const char * bmp_compression_str(const enum bmp_compression comp);

const char * bmp_type_str(const enum bmp_type type);

void bmp_cleanup(struct bmp_desc *desc);

unsigned char * bmp_decode(const struct bmp_desc *desc);

enum lib_fail bmp_parse_header(struct bmp_desc *desc);

enum lib_fail bmp_open_file(FILE *ifp, struct bmp_desc *desc);

#endif /* LIB_BMP */

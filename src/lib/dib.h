#ifndef LIB_BMP
#define LIB_BMP

#include <stdio.h>
#include <stdlib.h>

#include "../raster/lib.h"
#include "../raster/pal.h"

enum dib_os2_compression {
	os2_no_compression = 0,
	os2_8bit_rle = 1,
	os2_4bit_rle = 2,
	os2_1d_huffman = 3,
	os2_24bit_rle = 4,
};

enum dib_compression {
	dib_no_compression = 0,
	dib_8bit_rle = 1,
	dib_4bit_rle = 2,
	dib_bitfield = 3,
};

enum dib_order {
	dib_bottom_up,
	dib_top_down,
};

enum dib_type {
	dib_core_header = 12,
	dib_info_header = 40,
	dib_v2_info_header = 52,
	dib_v3_info_header = 56,
	dib_v4_header = 108,
	dib_v5_header = 124,
};

struct dib_bitfield {
	uint32_t shift;
	uint32_t mask;
	uint32_t scale;
};

struct dib_desc {
	FILE *ifp;
	struct raster_desc r;

	enum trit is_os2:8;

	unsigned char depth;
	enum dib_type type:8;
	enum dib_order order:8;
	enum dib_compression compression:8;
	uint32_t pal_entries;
	struct dib_bitfield bf[4];

	size_t size;
};

const char * dib_compression_str(enum dib_compression comp);

const char * dib_type_str(enum dib_type type);

void dib_cleanup(struct dib_desc *desc);

unsigned char * dib_decode(const struct dib_desc *desc);

enum lib_fail dib_open_file(struct dib_desc *desc, FILE *ifp);

enum lib_fail bmp_parse_header(struct dib_desc *desc);

enum lib_fail bmp_open_file(struct dib_desc *desc, FILE *ifp);

/* ICO functions */
enum ico_type {
	ico_icon = 1,
	ico_cursor = 2,
};

struct ico_image {
	uint16_t x, y;
	uint32_t size, offset;
};

struct ico_desc {
	struct dib_desc dib;
	struct ico_image *images;
	enum ico_type type:16;
	uint16_t count;
};

const char * ico_type_str(enum ico_type);

void ico_cleanup(struct ico_desc *desc);

unsigned char * ico_decode(struct ico_desc *desc);

enum lib_fail ico_set_image(struct ico_desc *desc, uint16_t i);

enum lib_fail ico_parse_header(struct ico_desc *desc);

enum lib_fail ico_open_file(struct ico_desc *desc, FILE *ifp);

#endif /* LIB_BMP */

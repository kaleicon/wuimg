#ifndef LIB_TGA
#define LIB_TGA

#include <stdio.h>
#include <stdbool.h>
#include <time.h>

#include "../common.h"
#include "../raster/lib.h"
#include "../raster/pix.h"

struct tga_metadata {
	unsigned char id_len;
	unsigned char id[255];

	struct tga_author {
		char name[41];
		char comment[324];
	} author;

	bool has_stamp;
	struct utc_time stamp;

	struct tga_job {
		char name[41];
		unsigned short hour, minute, second;
	} job;

	struct tga_software {
		char id[41];
		char version_letter;
		unsigned short version_number;
	} software;

	struct pix_rgba8 key_color;
	unsigned short pixel_numerator, pixel_denominator;
	unsigned short gamma_numerator, gamma_denominator;

	unsigned int stamp_offset;
};

enum tga_image_type {
	tga_no_image_data = 0,
	tga_colormap_data = 1,
	tga_truecolor_data = 2,
	tga_monochrome_data = 3,
	tga_colormap_rle = 9,
	tga_truecolor_rle = 10,
	tga_monochrome_rle = 11,
};

struct tga_colormap {
	unsigned int offset, len;
	unsigned char bitdepth;
	unsigned char bytedepth;
	struct raster_pal *pal;
};

struct tga_desc {
	FILE *ifp;
	unsigned int w, h;
	enum tga_image_type type:8;
	unsigned char bitdepth;
	unsigned char bytedepth;
	unsigned char attr_bits, orientation;
	unsigned char ch;
	bool expand_16bit;

	long data_start;
	size_t data_len;
	struct tga_colormap map;
	struct tga_metadata *meta;
};

void tga_cleanup(struct tga_desc *desc);

unsigned char * tga_decode_stamp(const struct tga_desc *desc,
unsigned int *width, unsigned int *height);

unsigned char * tga_decode(const struct tga_desc *desc);

struct raster_pal * tga_take_palette(struct tga_desc *desc);

bool tga_parse_footer(struct tga_desc *desc);

enum lib_fail tga_parse_header(struct tga_desc *desc);

enum lib_fail tga_open_file(FILE *ifp, struct tga_desc *desc, bool read_metadata);

#endif /* LIB_TGA */

#ifndef LIB_TGA
#define LIB_TGA

#include <stdio.h>
#include <stdbool.h>

enum tga_fail {
	tga_ok = 0,
	tga_unexpected_eof,
	tga_invalid_header,
	tga_unsupported_format,
	tga_no_image_data,
	tga_alloc_error,
};

struct tga_color_entry {
	unsigned char b, g, r, a;
};

struct tga_metadata {
	unsigned char id_len;
	unsigned char id[255];

	struct tga_author {
		char name[41];
		char comment[324];
	} author;

	struct tga_stamp {
		unsigned short month, day, year, hour, minute, second;
	} stamp;

	struct tga_job {
		char name[41];
		unsigned short hour, minute, second;
	} job;

	struct tga_software {
		char id[41];
		char version_letter;
		unsigned short version_number;
	} software;

	struct tga_color_entry key_color;
	unsigned short pixel_numerator, pixel_denominator;
	unsigned short gamma_numerator, gamma_denominator;

	unsigned int stamp_offset;
};

enum tga_image_type {
	no_image_data = 0,
	colormap_data = 1,
	truecolor_data = 2,
	monochrome_data = 3,
	colormap_rle = 9,
	truecolor_rle = 10,
	monochrome_rle = 11,
};

struct tga_colormap {
	unsigned int offset, len;
	unsigned char bitdepth;
	unsigned char bytedepth;
	struct tga_color_entry *entry;
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

const char * tga_fail_string(const enum tga_fail fail);

void tga_cleanup(struct tga_desc *desc);

unsigned char * tga_decode_stamp(const struct tga_desc *desc,
unsigned int *width, unsigned int *height);

unsigned char * tga_decode(const struct tga_desc *desc);

struct tga_color_entry * tga_take_palette(struct tga_desc *desc);

bool tga_parse_footer(struct tga_desc *desc);

enum tga_fail tga_parse_header(struct tga_desc *desc);

enum tga_fail tga_open_file(FILE *ifp, struct tga_desc *desc,
const bool read_id);

#endif /* LIB_TGA */

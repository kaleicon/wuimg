#ifndef LIB_TGA
#define LIB_TGA

#include <stdio.h>
#include <stdbool.h>
#include <time.h>

#include "wudefs.h"
#include "raster/pix.h"

struct tga_metadata {
	unsigned char id_len;
	unsigned char id[255];

	struct tga_author {
		char name[41];
		char comment[324];
	} author;

	time_t timestamp;

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
	struct raster_pal *extra_pal;
	unsigned int offset, len;
	unsigned char depth;
};

struct tga_desc {
	FILE *ifp;
	enum tga_image_type type:8;
	unsigned char depth;

	long data_start;
	struct tga_colormap map;
	struct tga_metadata meta;
};

const char * tga_type_str(enum tga_image_type type);

void tga_cleanup(struct tga_desc *desc);

size_t tga_decode_stamp(const struct tga_desc *desc, struct raw_img *stamp);

size_t tga_decode(const struct tga_desc *desc, struct raw_img *img);

struct raster_pal * tga_take_extra_palette(struct tga_desc *desc);

enum wu_error tga_parse_stamp(const struct tga_desc *desc,
struct raw_img *main, struct raw_img *stamp);

bool tga_parse_footer(struct tga_desc *desc, struct raw_img *img);

enum wu_error tga_parse_header(struct tga_desc *desc, struct raw_img *img,
FILE *ifp);

#endif /* LIB_TGA */

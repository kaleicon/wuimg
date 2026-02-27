// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2020 kaleido
#ifndef LIB_TGA
#define LIB_TGA

#include <stdio.h>
#include <stdbool.h>
#include <time.h>

#include "raster/wuimg.h"
#include "raster/pix.h"

enum tga_attr_type {
	tga_attr_ignore = 0,
	tga_attr_undefined_ignorable = 1,
	tga_attr_undefined_important = 2,
	tga_attr_useful_alpha = 3,
	tga_attr_associated_alpha = 4,
};

struct tga_ratio {
	uint16_t num, den;
};

struct tga_metadata {
	unsigned char id_len;
	unsigned char id[255];

	struct tga_author {
		char name[41];
		char comment[324];
	} author;

	enum tga_attr_type attr:8;

	time_t timestamp;

	struct tga_job {
		char name[41];
		uint16_t hour, minute, second;
	} job;

	struct tga_software {
		char id[41];
		char version_letter;
		uint16_t version_number;
	} software;

	struct pix_rgba8 key_color;

	struct tga_ratio pixel_ratio;
	struct tga_ratio gamma;

	uint32_t color_correction_offset;
	uint32_t stamp_offset;
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
	struct palette *pal;
	uint16_t offset, len;
	uint8_t depth;
	bool use;
};

struct tga_desc {
	FILE *ifp;
	enum tga_image_type type:8;
	uint8_t depth;
	uint8_t img_desc;
	bool ext_area;
	uint16_t x, y, w, h;

	long data_start;
	struct tga_colormap map;
	struct tga_metadata meta;
};

const char * tga_attr_type_str(enum tga_attr_type type);

const char * tga_type_str(enum tga_image_type type);

void tga_cleanup(struct tga_desc *desc);

struct wu_st tga_load_stamp(const struct tga_desc *desc, struct wuimg *stamp);

struct wu_st tga_decode(const struct tga_desc *desc, struct wuimg *img);

struct wu_st tga_img_info(struct tga_desc *desc, struct wuimg *img);

struct wu_st tga_parse_stamp(struct tga_desc *desc, struct wuimg *stamp);

bool tga_parse_footer(struct tga_desc *desc);

struct wu_st tga_parse_header(struct tga_desc *desc, FILE *ifp);

#endif /* LIB_TGA */

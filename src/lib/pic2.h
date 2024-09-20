// SPDX-License-Identifier: 0BSD
#ifndef LIB_PIC2
#define LIB_PIC2

#include "raster/wuimg.h"

enum pic2_id {
	pic2_end = 0, // End of File
	pic2_p2bi = FOURCC('P', '2', 'B', 'I'), // Raw, little-endian (Intel)
	pic2_p2bm = FOURCC('P', '2', 'B', 'M'), // Raw, big-endian (Motorola)
	pic2_p2sf = FOURCC('P', '2', 'S', 'F'), // Run-length encoding
	pic2_p2ss = FOURCC('P', '2', 'S', 'S'), // Arithmetic encoding
	pic2_pdpi = FOURCC('P', 'D', 'P', 'I'), // Image DPI
};

struct pic2_image {
	uint8_t depth; // Copied from header
	uint16_t w, h, x, y;
	uint32_t opaque;
	struct wuptr data;
};

struct pic2_block {
	enum pic2_id id;
	bool is_image;
	union {
		uint16_t dpi;
		struct pic2_image image;
	} u;
};

struct pic2_desc {
	struct mparser mp;
	struct wuptr text;
	struct wuptr name, subtitle, title, saver;
	struct wuptr comment;
	time_t created;
	uint16_t w, h;
	uint16_t x_aspect, y_aspect;
	uint16_t image_number;
	uint8_t depth;
	uint8_t pal_depth;

	size_t start;
};

const char * pic2_encoding_str(enum pic2_id id);

void pic2_rewind(struct pic2_desc *desc);

size_t pic2_decode(const struct pic2_block *block, struct wuimg *img);

enum wu_error pic2_set_image(const struct pic2_desc *desc,
const struct pic2_block *block, struct wuimg *img);

enum wu_error pic2_next_block(struct pic2_desc *desc, struct pic2_block *block);

enum wu_error pic2_parse(struct pic2_desc *desc);

enum wu_error pic2_init(struct pic2_desc *desc, const struct map_info map);

#endif // LIB_PIC2

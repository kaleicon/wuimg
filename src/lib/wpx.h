// SPDX-License-Identifier: 0BSD
#ifndef LIB_WPX
#define LIB_WPX

#include <stdio.h>

#include "raster/wuimg.h"
#include "misc/memparser.h"

enum wpx_section_id {
	wpx_bmp_info = 0x10,
	wpx_bmp_data = 0x11,
	wpx_bmp_palette = 0x12,
	wpx_bmp_mask = 0x13,

	wpx_ia2_count = 0x30,
	wpx_ia2_offsets = 0x31,
	wpx_ia2_files = 0x32,

	wpx_ia2_geom = 0x33,
	wpx_ia2_mys4 = 0x34,
	wpx_ia2_mys5 = 0x35,
	wpx_ia2_name_idx = 0x36,
	wpx_ia2_names = 0x37,
	wpx_ia2_sfx_idx = 0x38,
	wpx_ia2_sfx = 0x39,
};

struct wpx_section {
	enum wpx_section_id id:8;
	uint8_t fmt;
	uint16_t __pad;
	uint32_t offset;
	uint32_t decomp_size;
	uint32_t comp_size;
};

struct wpx_dir {
	uint8_t count;
	struct wpx_section *sections;
};

struct wpx_bmp_desc {
	struct mp_parser mp;
	struct wpx_dir dir;
	int raster_idx;
	int mask_idx;
	uint8_t depth;
	struct raster_pal *pal;
};


struct wpx_ia2_array {
	uint32_t nr;
	uint32_t *val;
};

struct wpx_ia2_list {
	uint32_t str_len;
	char *str;
	struct wpx_ia2_array idx;
};

struct wpx_ia2_desc {
	struct mp_parser mp;
	struct wpx_dir dir;
	uint32_t base;
	struct wpx_ia2_array frames;
	struct wpx_ia2_array mys3;
	struct wpx_ia2_array mys4;
	struct wpx_ia2_array mys5;
	struct wpx_ia2_list names;
	struct wpx_ia2_list sfx;
};

void wpx_bmp_cleanup(struct wpx_bmp_desc *desc);

size_t wpx_bmp_decode(const struct wpx_bmp_desc *desc, struct wuimg *img);

enum wu_error wpx_bmp_parse(struct wpx_bmp_desc *desc, struct wuimg *img);

enum wu_error wpx_bmp_open(struct wpx_bmp_desc *desc, struct mp_parser mp);


void wpx_ia2_cleanup(struct wpx_ia2_desc *desc);

enum wu_error wpx_ia2_set_frame(const struct wpx_ia2_desc *desc,
struct wpx_bmp_desc *frame, uint32_t i);

bool wpx_ia2_list_get(const struct wpx_ia2_list *list, uint32_t idx,
struct wuptr *str);

enum wu_error wpx_ia2_parse(struct wpx_ia2_desc *desc);

enum wu_error wpx_ia2_open(struct wpx_ia2_desc *desc, struct mp_parser mp);

#endif /* LIB_WPX */

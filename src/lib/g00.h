// SPDX-License-Identifier: 0BSD
#ifndef LIB_G00
#define LIB_G00

#include "misc/mparser.h"
#include "raster/wuimg.h"

struct g00_desc_v1 {
	uint16_t pal_entries;
};

struct g00_desc_v2 {
	uint32_t dir_count;
	const uint8_t *dir;
};

enum g00_version {
	g00_v0 = 0,
	g00_v1 = 1,
	g00_v2 = 2,
};

struct g00_desc {
	struct mparser mp;
	enum g00_version version;
	uint32_t comp_size;
	size_t decomp_size;

	union {
		uint8_t *buf;
		struct g00_desc_v2 v2;
	} u;
};

void g00_cleanup(struct g00_desc *desc, struct wuimg *img);

enum wu_error g00_decode(struct g00_desc *desc, struct wuimg *img);

enum wu_error g00_parse(struct g00_desc *desc, struct wuimg *img,
struct map_info map);

#endif /* LIB_G00 */

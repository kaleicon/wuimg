// SPDX-License-Identifier: 0BSD
#ifndef LIB_HG3
#define LIB_HG3

#include "raster/wuimg.h"
#include "misc/memparser.h"

struct hg3_desc {
	struct mp_parser mp;
	struct mp_parser image;
	int32_t x, y;
	uint32_t canvas_w, canvas_h;
};

bool hg3_decode(const struct hg3_desc *desc, struct wuimg *img);

enum wu_error hg3_parse_image(struct hg3_desc *desc, struct wuimg *img);

enum wu_error hg3_next_image(struct hg3_desc *desc);

enum wu_error hg3_open(struct hg3_desc *desc, struct mp_parser mp);

#endif /* LIB_HG3 */

// SPDX-License-Identifier: 0BSD
#ifndef LIB_HG3
#define LIB_HG3

#include "raster/wuimg.h"
#include "misc/mparser.h"

struct hg3_desc {
	struct mparser mp;
	struct mparser image;
	int32_t x, y;
	uint32_t canvas_w, canvas_h;
};

bool hg3_decode(const struct hg3_desc *desc, struct wuimg *img);

enum wu_error hg3_parse_image(struct hg3_desc *desc, struct wuimg *img);

enum wu_error hg3_next_image(struct hg3_desc *desc);

enum wu_error hg3_open(struct hg3_desc *desc, struct mparser mp);

#endif /* LIB_HG3 */

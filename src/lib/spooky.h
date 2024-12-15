// SPDX-License-Identifier: 0BSD
#ifndef LIB_SPOOKY
#define LIB_SPOOKY

#include "raster/wuimg.h"
#include "misc/mparser.h"

struct tre_desc {
	struct mparser mp;
	uint32_t chunks;
};

size_t tre_decode(const struct tre_desc *desc, struct wuimg *img);

enum wu_error tre_parse(struct tre_desc *desc, struct wuimg *img);

enum wu_error tre_init(struct tre_desc *desc, struct wuptr mem);


struct trs_desc {
	struct mparser mp;
	uint16_t nr;
	uint16_t xres;
	const uint8_t *sprites;
};

size_t trs_get_image(const struct trs_desc *desc, struct wuimg *img, uint16_t i);

enum wu_error trs_set_image(struct trs_desc *desc, struct wuimg *img, uint16_t i);

enum wu_error trs_init(struct trs_desc *desc, struct wuptr mem);

#endif // LIB_SPOOKY

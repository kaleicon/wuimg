// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2024 kaleido
#ifndef LIB_SPOOKY
#define LIB_SPOOKY

#include "raster/wuimg.h"
#include "misc/mparser.h"

struct tre_desc {
	uint32_t chunks;
	struct wuptr data;
};

struct wu_st tre_decode(const struct tre_desc *desc, struct wuimg *img);

struct wu_st tre_parse(struct tre_desc *desc, struct wuimg *img,
struct wuptr mem);


struct trs_desc {
	struct mparser mp;
	uint16_t nr;
	uint16_t xres;
	const uint8_t *sprites;
};

struct wu_st trs_get_image(const struct trs_desc *desc, struct wuimg *img,
uint16_t i);

struct wu_st trs_set_image(const struct trs_desc *desc, struct wuimg *img,
uint16_t i);

struct wu_st trs_parse(struct trs_desc *desc, struct wuptr mem);

#endif // LIB_SPOOKY

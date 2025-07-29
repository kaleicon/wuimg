// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2025 kaleido
#ifndef LIB_UTAHRLE
#define LIB_UTAHRLE

#include "raster/wuimg.h"
#include "misc/mparser.h"

struct utah_desc {
	struct mparser mp;
	int16_t x, y;
	bool clear, alpha;
	uint8_t channels;
	uint8_t pal_ch, pal_len;
	const uint8_t *pal;
	const uint8_t *bg;
	struct wuptr comm;
};

struct wu_st utah_decode(struct utah_desc *desc, struct wuimg *img);

bool utah_next_comment(const struct utah_desc *desc, struct wuimg *img,
size_t *pos, struct wuptr *key, struct wuptr *val);

struct wu_st utah_parse(struct utah_desc *desc, struct wuimg *img,
struct wuptr mem);

#endif /* LIB_UTAHRLE */

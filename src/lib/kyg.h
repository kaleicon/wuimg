// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2025 kaleido
#ifndef LIB_KYG
#define LIB_KYG

#include "raster/wuimg.h"

struct kyg_desc {
	struct mparser mp;
	struct wuptr comment;
	uint16_t x, y;
	uint32_t len;
};

size_t kyg_decode(const struct kyg_desc *desc, struct wuimg *img);

enum wu_error kyg_parse(struct kyg_desc *desc, struct wuimg *img);

enum wu_error kyg_identify(struct kyg_desc *desc, struct wuptr map);

#endif /* LIB_KYG */

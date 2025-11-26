// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2024 kaleido
#ifndef LIB_Q4
#define LIB_Q4

#include "misc/mparser.h"
#include "raster/wuimg.h"

struct q4_desc {
	struct mparser mp;
	uint32_t creation_date;
	int skip;
};

time_t q4_approximate_date(const struct q4_desc *desc);

size_t q4_decode(const struct q4_desc *desc, struct wuimg *img);

enum wu_error q4_open(struct q4_desc *desc, struct wuptr mem);

enum wu_error q4_info(struct wuimg *img);

#endif // LIB_Q4

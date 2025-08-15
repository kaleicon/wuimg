// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2025 kaleido
#ifndef LIB_SGF
#define LIB_SGF

#include "misc/mparser.h"
#include "raster/wuimg.h"

struct sgf_desc {
	struct mparser mp;
	size_t nr;
	size_t idx;
};

struct wu_st sgf_decode(struct sgf_desc *desc, struct wuimg *img);

struct wu_st sgf_setup_next(struct sgf_desc *desc, struct wuimg *img);

struct wu_st sgf_parse(struct sgf_desc *desc, struct wuptr mem);

#endif /* LIB_SGF */

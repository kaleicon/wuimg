// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2026 kaleido
#ifndef LIB_KABOOM
#define LIB_KABOOM

#include "misc/iff.h"
#include "misc/mparser.h"
#include "raster/wuimg.h"

struct bmb_desc {
	FILE *ifp;
	struct iff_state iff;
	struct mparser idx2;
	struct wuimg *img;
	uint32_t imag_size;
	uint32_t mipmaps;
	uint32_t nr;
	uint32_t i;
};

void bmb_cleanup(struct bmb_desc *desc);

struct wu_st bmb_decode(struct bmb_desc *desc, struct wuimg *img);

struct wu_st bmb_parse_next(struct bmb_desc *desc, struct wuimg *img);

struct wu_st bmb_init(struct bmb_desc *desc, FILE *ifp);

#endif /* LIB_KABOOM */

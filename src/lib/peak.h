// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2025 kaleido
#ifndef LIB_PEAK
#define LIB_PEAK

#include "raster/wuimg.h"

struct peak_desc {
	uint32_t channels;
	uint32_t sample_size;
	uint32_t sample_offset;
	bool high_depth;
	bool lo_after;
	struct wuptr mem;
};

struct wu_st peak_graph(const struct peak_desc *desc, struct wuimg *img);

struct wu_st peak_init(struct peak_desc *desc, struct wuimg *img,
struct wuptr mem);
struct wu_st rpkn_init(struct peak_desc *desc, struct wuimg *img,
struct wuptr mem);
struct wu_st sfpk_init(struct peak_desc *desc, struct wuimg *img,
struct wuptr mem);

#endif /* LIB_PEAK */

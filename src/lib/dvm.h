// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2025 kaleido
#ifndef LIB_DVM
#define LIB_DVM

#include "raster/wuimg.h"

enum dvm_pal {
	dvm_pal_standard = 0,
	dvm_pal_per_frame = 1,
	dvm_pal_global = 2,
};

struct dvm_desc {
	FILE *ifp;
	uint8_t version;
	uint8_t depth;
	enum dvm_pal pal:8;
	struct wustr text;
	long off;
	size_t frame_size;
};

void dvm_cleanup(struct dvm_desc *desc);

struct wu_st dvm_load_frame(struct dvm_desc *desc, struct wuimg *img, size_t i);

struct wu_st dvm_parse(struct dvm_desc *desc, struct wuimg *img, FILE *ifp);

#endif /* LIB_DVM */

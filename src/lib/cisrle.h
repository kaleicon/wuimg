// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2025 kaleido
#ifndef LIB_CISRLE
#define LIB_CISRLE

#include "raster/wuimg.h"

enum cis_resolution {
	cis_high_res = 'H',
	cis_medium_res = 'M',
};

struct cis_desc {
	struct wuptr data;
	enum cis_resolution res;
	bool swap;
};

const char * cis_resolution_str(enum cis_resolution res);

struct wu_st cis_decode(const struct cis_desc *desc, struct wuimg *img);

struct wu_st cis_parse(struct cis_desc *desc, struct wuimg *img,
struct wuptr mem, bool try_fix);

#endif /* LIB_CISRLE */

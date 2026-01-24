// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2022 kaleido
#ifndef LIB_PDT
#define LIB_PDT

#include "raster/wuimg.h"
#include "misc/mparser.h"

enum pdt_version {
	pdt10 = '0',
	pdt11 = '1',
};

struct pdt_desc {
	struct mparser mp;
	enum pdt_version version;
	uint32_t mask_offset;
	struct palette *pal;
};

const char * pdt_version_str(enum pdt_version version);

void pdt_cleanup(struct pdt_desc *desc);

struct wu_st pdt_decode(const struct pdt_desc *desc, struct wuimg *img);

struct wu_st pdt_init(struct pdt_desc *desc, struct wuimg *img,
struct wuptr mem);

#endif /* LIB_PDT */

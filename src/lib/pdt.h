// SPDX-License-Identifier: 0BSD
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

size_t pdt_decode(const struct pdt_desc *desc, struct wuimg *img);

enum wu_error pdt_parse_header(struct pdt_desc *desc, struct wuimg *img);

enum wu_error pdt_open_mem(struct pdt_desc *desc, struct wuptr mem);

#endif /* LIB_PDT */

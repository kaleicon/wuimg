// SPDX-License-Identifier: 0BSD
#ifndef LIB_TLG
#define LIB_TLG

#include "raster/wuimg.h"
#include "misc/mparser.h"

enum tlg_version {
	tlg_v5 = '5',
	tlg_v6 = '6',
};

struct tlg_desc {
	struct mparser mp;
	bool tagged_data;
	enum tlg_version version:8;
	uint32_t block_height;
};

const char * tlg_version_str(enum tlg_version ver);

size_t tlg_decode(const struct tlg_desc *desc, struct wuimg *img);

enum wu_error tlg_read_header(struct tlg_desc *desc, struct wuimg *img);

enum wu_error tlg_open_mem(struct tlg_desc *desc, struct wuptr mem);

#endif /* LIB_TLG */

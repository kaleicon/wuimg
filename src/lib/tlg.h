// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2022 kaleido
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

struct wu_st tlg_decode(const struct tlg_desc *desc, struct wuimg *img);

struct wu_st tlg_read_header(struct tlg_desc *desc, struct wuptr mem,
struct wuimg *img);

#endif /* LIB_TLG */

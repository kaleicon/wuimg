// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2020 kaleido
#ifndef LIB_MAC
#define LIB_MAC

#include <stdio.h>
#include <stdbool.h>

#include "raster/wuimg.h"

typedef uint32_t mac_time_t;

struct mac_binary_header {
	uint8_t name_len;
	uint8_t name[63];

	uint8_t type[4];
	uint8_t creator[4];
	uint8_t attributes;
	uint8_t protection;

	struct window_info {
		uint16_t id;
		uint16_t y, x;
	} window;
	struct timestamp {
		mac_time_t created, modified;
	} time;
};

struct mac_desc {
	uint8_t version;
	bool has_patterns;
	bool has_macbin_header;
	struct mac_binary_header macbin;
	struct wuptr pat, rle;
};

time_t mac_time_to_unix(mac_time_t time);

struct wu_st mac_decode(const struct mac_desc *desc, struct wuimg *main);

struct wu_st mac_patterns_load(const struct mac_desc *desc, struct wuimg *pats);

void mac_get_sizes(struct wuimg *main, struct wuimg *pats);

struct wu_st mac_open_file(struct mac_desc *desc, struct wuptr mem);

#endif /* LIB_MAC */

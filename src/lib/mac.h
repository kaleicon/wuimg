// SPDX-License-Identifier: 0BSD
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
	FILE *ifp;

	uint8_t version;
	bool has_patterns;
	bool has_macbin_header;
	struct mac_binary_header macbin;
};

time_t mac_time_to_unix(mac_time_t time);

size_t mac_decode(const struct mac_desc *desc, struct wuimg *main);

size_t mac_patterns_load(const struct mac_desc *desc, struct wuimg *pats);

enum wu_error mac_get_sizes(struct wuimg *main, struct wuimg *pats);

enum wu_error mac_open_file(struct mac_desc *desc, FILE *ifp);

#endif /* LIB_MAC */

#ifndef LIB_MAC
#define LIB_MAC

#include <stdio.h>
#include <stdbool.h>

#include "../raster/lib.h"

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
		uint32_t created, modified;
	} time;
};

struct mac_desc {
	FILE *ifp;

	struct raster_desc patterns;
	struct raster_desc rast;

	uint32_t version;
	bool has_macbin_header;

	struct mac_binary_header macbin;
};

unsigned char * mac_decode(const struct mac_desc *desc);

size_t mac_patterns_load(const struct mac_desc *desc, void *restrict dst);

enum lib_fail mac_open_file(struct mac_desc *desc, FILE *ifp);

#endif /* LIB_MAC */

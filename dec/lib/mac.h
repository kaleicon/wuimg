#ifndef LIB_MAC
#define LIB_MAC

#include <stdio.h>
#include <stdbool.h>

#include "common/lib.h"
#include "../../common.h"

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

	uint32_t version;
	bool has_macbin_header;

	struct mac_binary_header macbin;
};

unsigned char * mac_pattern_unpack(struct mac_desc *desc);

unsigned char * mac_decode(struct mac_desc *desc);

enum lib_fail mac_open_file(FILE *ifp, struct mac_desc *desc);

#endif /* LIB_MAC */

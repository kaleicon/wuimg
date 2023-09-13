// SPDX-License-Identifier: 0BSD
#ifndef LIB_PGX
#define LIB_PGX

#include <stdio.h>
#include <stdint.h>

#include "raster/wuimg.h"

struct pgx_desc {
	FILE *ifp;
	uint32_t comp_size;
};

size_t pgx_decode(const struct pgx_desc *desc, struct wuimg *img);

enum wu_error pgx_read_header(struct pgx_desc *desc, struct wuimg *img);

enum wu_error pgx_open_file(struct pgx_desc *desc, FILE *ifp);

#endif /* LIB_PGX */

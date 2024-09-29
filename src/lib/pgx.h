// SPDX-License-Identifier: 0BSD
#ifndef LIB_PGX
#define LIB_PGX

#include "misc/mparser.h"
#include "raster/wuimg.h"

struct pgx_desc {
	struct mparser mp;
	uint32_t comp_size;
};

size_t pgx_decode(const struct pgx_desc *desc, struct wuimg *img);

enum wu_error pgx_read_header(struct pgx_desc *desc, struct wuimg *img);

enum wu_error pgx_init(struct pgx_desc *desc, struct map_info map);

#endif /* LIB_PGX */

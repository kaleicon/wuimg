#ifndef LIB_PGX
#define LIB_PGX

#include <stdio.h>
#include <stdint.h>

#include "../raster/lib.h"

struct pgx_desc {
	FILE *ifp;
	struct raster_desc rast;
	uint32_t comp_size;
	bool transparent;
};

size_t pgx_decode(const struct pgx_desc *desc, void *restrict dst);

enum lib_fail pgx_read_header(struct pgx_desc *desc);

enum lib_fail pgx_open_file(struct pgx_desc *desc, FILE *ifp);

#endif /* LIB_PGX */

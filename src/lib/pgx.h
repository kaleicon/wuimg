#ifndef LIB_PGX
#define LIB_PGX

#include <stdio.h>
#include <stdint.h>

#include "../raster/lib.h"

struct pgx_desc {
	FILE *ifp;
	struct raster_desc rast;
	bool transparent;
	uint32_t compressed_size;
};

unsigned char * pgx_decode(const struct pgx_desc *desc);

enum lib_fail pgx_read_header(struct pgx_desc *desc);

enum lib_fail pgx_open_file(FILE *ifp, struct pgx_desc *desc);

#endif /* LIB_PGX */

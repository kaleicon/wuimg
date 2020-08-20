#ifndef LIB_PGX
#define LIB_PGX

#include <stdio.h>
#include <stdint.h>

#include "common_lib.h"

struct pgx_desc {
	FILE *ifp;
	uint32_t width, height;
	uint32_t compressed_size;
	bool transparent;
};

unsigned char * pgx_decode(const struct pgx_desc *desc);

enum lib_fail pgx_read_header(struct pgx_desc *desc);

enum lib_fail pgx_open_file(FILE *ifp, struct pgx_desc *desc);

#endif /* LIB_PGX */

#ifndef LIB_PNM
#define LIB_PNM

#include <stdio.h>
#include <stdbool.h>

#include "../common.h"
#include "../raster/lib.h"

enum pnm_type {
	pnm_plain_pbm = '1',
	pnm_plain_pgm = '2',
	pnm_plain_ppm = '3',
	pnm_raw_pbm = '4',
	pnm_raw_pgm = '5',
	pnm_raw_ppm = '6',

	pnm_pam = '\n',
	pnm_xv_thumb = ' ',
	pnm_mtv = 1,
	pnm_color_pfm = 'F',
	pnm_gray_pfm = 'f',
};

struct pnm_desc {
	FILE *ifp;
	struct raster_desc rast;

	size_t nr;
	long data_start;
	union {
		unsigned short pnm;
		float pfm;
	} scale;
	unsigned char bytedepth;
	enum pnm_type type:8;
	enum endianness pfm_endian:8;
};

const char * pnm_type_str(enum pnm_type type);

size_t pnm_decode(const struct pnm_desc *desc, void *restrict dst, size_t i);

enum lib_fail pnm_parse_header(struct pnm_desc *desc);

enum lib_fail pnm_open_file(struct pnm_desc *desc, FILE *ifp, bool maybe_mtv);

#endif /* LIB_PNM */

#ifndef LIB_PNM
#define LIB_PNM

#include <stdio.h>
#include <stdbool.h>

#include "common.h"
#include "common_lib.h"

enum pnm_type {
	plain_pbm = '1',
	plain_pgm = '2',
	plain_ppm = '3',
	raw_pbm = '4',
	raw_pgm = '5',
	raw_ppm = '6',
	pam = '\n',

	xv_thumb = ' ',
	mtv = 1,
	color_pfm = 'F',
	gray_pfm = 'f',
};

struct pnm_desc {
	FILE *ifp;

	size_t nr;
	size_t w, h;
	unsigned short maxval;
	unsigned char ch, bytedepth;
	float pfm_scale;
	enum endianness pfm_endian;
	enum pnm_type type:8;
	bool xv_no_expand;
};

unsigned char * pnm_decode_next(const struct pnm_desc *desc);

enum lib_fail pnm_parse_header(struct pnm_desc *desc);

enum lib_fail pnm_open_file(FILE *ifp, struct pnm_desc *desc, bool maybe_mtv);

#endif /* LIB_PNM */

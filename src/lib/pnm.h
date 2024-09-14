// SPDX-License-Identifier: 0BSD
#ifndef LIB_PNM
#define LIB_PNM

#include <stdio.h>
#include <stdbool.h>

#include "raster/wuimg.h"
#include "misc/endian.h"

enum pnm_type {
	pnm_plain_pbm = '1',
	pnm_plain_pgm = '2',
	pnm_plain_ppm = '3',
	pnm_raw_pbm = '4',
	pnm_raw_pgm = '5',
	pnm_raw_ppm = '6',

	pnm_pam = '\n',
	pnm_xv_thumb = ' ',
	pnm_color_pfm = 'F',
	pnm_gray_pfm = 'f',
	pnm_color_phm = 'H',
	pnm_gray_phm = 'h',
	pnm_mtv = 1,

	/* PGX format, defined in JPEG2000 Part 4 Annex B.2.6 for conformance
	 * testing.
	 * Though not explicitly related to other PNM formats, it's so similar
	 * it's easier to lump it here. */
	pnm_pgx = 'G',
};

struct pnm_desc {
	FILE *ifp;
	struct wuimg rast;

	size_t nr;
	long data_start;
	union {
		unsigned pnm;
		float pfm;
	} scale;
	unsigned char bytedepth;
	enum pnm_type type:8;
	enum endianness endian:8;
	bool skip_scaling;
};

const char * pnm_type_str(enum pnm_type type);

size_t pnm_decode(struct pnm_desc *desc, struct wuimg *img, size_t i);

enum wu_error pnm_parse_header(struct pnm_desc *desc);

enum wu_error pnm_open_file(struct pnm_desc *desc, FILE *ifp, bool maybe_mtv);

#endif /* LIB_PNM */

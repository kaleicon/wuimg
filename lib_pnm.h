#ifndef LIB_PNM
#define LIB_PNM

#include <inttypes.h>
#include <stdbool.h>

enum pnm_fail {
	pnm_ok = 0,
	pnm_unexpected_eof,
	pnm_unknown_format,
	pnm_invalid_header,
	pnm_unsupported_tuple,
	pnm_alloc_error,
};

enum pnm_type {
	plain_pbm = '1',
	plain_pgm = '2',
	plain_ppm = '3',
	raw_pbm = '4',
	raw_pgm = '5',
	raw_ppm = '6',
	pam = '\n',

	mtv = 1,
	xv_thumb = ' ',
//	color_pfm = 'F',
//	gray_pfm = 'f',
};

struct pnm_desc {
	FILE *ifp;

	size_t nr;
	size_t w, h;
	unsigned short maxval;
	unsigned char ch, bytedepth;
	enum pnm_type type:8;
	bool swap;
	bool xv_no_expand;
};

const char * pnm_fail_string(const enum pnm_fail fail);

unsigned char * pnm_decode_next(const struct pnm_desc *desc);

enum pnm_fail pnm_parse_header(struct pnm_desc *desc);

enum pnm_fail pnm_open_file(FILE *ifp, struct pnm_desc *desc,
const bool maybe_mtv);

#endif /* LIB_PNM */

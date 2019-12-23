#ifndef LIB_NETPNM
#define LIB_NETPNM

#include <inttypes.h>
#include <stdbool.h>

enum pnm_format {
	plain_pbm = 1,
	plain_pgm = 2,
	plain_ppm = 3,
	raw_pbm = 4,
	raw_pgm = 5,
	raw_ppm = 6,
	pam = 7,
};

struct pnm_desc {
	size_t w, h, nr;
	unsigned int ch, depth, maxval;
	enum pnm_format type;

	// Internal use
	unsigned char *map;
	long file_size;
	uint_least32_t scale;
};

void close_pnm_file(const struct pnm_desc *desc);

unsigned char * decode_pnm_next(struct pnm_desc *desc);

bool parse_pnm_header(struct pnm_desc *desc);

enum pnm_format open_pnm_file(const char *filename, struct pnm_desc *desc);

#endif /* LIB_NETPNM */

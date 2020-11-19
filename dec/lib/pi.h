#ifndef LIB_PI
#define LIB_PI

#include <stdio.h>

#include "common/lib.h"
#include "common/unpack.h"

enum pi_repeat_src {
	pi_last4 = 0,
	pi_1row = 1,
	pi_2row = 2,
	pi_1row_next = 6,
	pi_1row_prev = 7,
};

struct pi_desc {
	FILE *ifp;
	struct colormap *palette;

	unsigned char *comment;
	unsigned char *restrict saver;
	unsigned short comment_len;
	unsigned short comment_area_len;
	unsigned short saver_len;
	unsigned char saver_sig[4];

	unsigned char pixel_x, pixel_y;

	unsigned int w, h;
	unsigned char bitdepth;
};

void pi_cleanup(struct pi_desc *desc);

unsigned char * pi_decode(const struct pi_desc *desc);

unsigned char * pi_take_palette(struct pi_desc *desc);

enum lib_fail pi_read_header(struct pi_desc *desc);

enum lib_fail pi_open_file(FILE *ifp, struct pi_desc *desc);

#endif /* LIB_PI */

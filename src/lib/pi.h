#ifndef LIB_PI
#define LIB_PI

#include <stdio.h>

#include "wudefs.h"

struct pi_saver {
	unsigned char *data;
	unsigned short len;
	unsigned char sig[4];
};

struct pi_comment {
	unsigned char *data;
	unsigned short text_len;
	unsigned short area_len;
};

struct pi_desc {
	FILE *ifp;
	unsigned char depth;
	unsigned char pixel_x, pixel_y;

	struct pi_comment comment;
	struct pi_saver saver;
};

void pi_cleanup(struct pi_desc *desc);

size_t pi_decode(const struct pi_desc *desc, struct raw_img *img);

enum wu_error pi_read_header(struct pi_desc *desc, struct raw_img *img);

enum wu_error pi_open_file(struct pi_desc *desc, FILE *ifp);

#endif /* LIB_PI */

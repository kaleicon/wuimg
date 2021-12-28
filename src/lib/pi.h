#ifndef LIB_PI
#define LIB_PI

#include <stdio.h>

#include "../raster/lib.h"

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
	struct raster_desc rast;
	unsigned char depth;
	unsigned char pixel_x, pixel_y;

	struct pi_comment comment;
	struct pi_saver saver;
};

void pi_cleanup(struct pi_desc *desc);

unsigned char * pi_decode(const struct pi_desc *desc);

enum lib_fail pi_read_header(struct pi_desc *desc);

enum lib_fail pi_open_file(struct pi_desc *desc, FILE *ifp);

#endif /* LIB_PI */

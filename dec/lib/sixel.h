#include <stdio.h>
#include <stdbool.h>

#include "common/lib.h"
#include "common/unpack.h"

enum sixel_background_color {
	set_to_bg = 0,
	retain = 1,
};

struct sixel_colormap {
	size_t active;
	struct colormap map[256];
};

struct sixel_desc {
	FILE *ifp;
	size_t data_len;
	unsigned char *data;

	size_t w, h;
	int pan, pad;
	enum sixel_background_color p2;
	unsigned char horizontal_grid_size;
};

void sixel_cleanup(struct sixel_desc *desc);

uint32_t * sixel_decode(struct sixel_desc *desc);

enum lib_fail sixel_calc_parameters(struct sixel_desc *desc);

enum lib_fail sixel_open_file(FILE *ifp, struct sixel_desc *desc);

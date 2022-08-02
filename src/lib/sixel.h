#include <stdio.h>
#include <stdbool.h>

#include "wudefs.h"
#include "raster/memparser.h"

enum sixel_background_color {
	sixel_set_to_bg = 0,
	sixel_retain = 1,
};

struct sixel_desc {
	struct mp_parser tp;
	size_t data_end;

	enum sixel_background_color p2;
	unsigned char horizontal_grid_size;
};

size_t sixel_decode(const struct sixel_desc *desc, struct raw_img *img);

enum wu_error sixel_calc_parameters(struct sixel_desc *desc,
struct raw_img *img);

enum wu_error sixel_open_mem(struct sixel_desc *desc,
const struct mp_parser mp);

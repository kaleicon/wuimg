#include <stdio.h>
#include <stdbool.h>

#include "../raster/lib.h"
#include "../raster/text.h"

enum sixel_background_color {
	sixel_set_to_bg = 0,
	sixel_retain = 1,
};

struct sixel_desc {
	struct text_parser tp;
	size_t data_end;

	struct raster_desc r;
	unsigned int pan, pad;
	enum sixel_background_color p2;
	unsigned char horizontal_grid_size;
};

size_t sixel_decode(const struct sixel_desc *desc, struct pix_rgba8 *dst);

enum lib_fail sixel_calc_parameters(struct sixel_desc *desc);

enum lib_fail sixel_open_mem(struct sixel_desc *desc,
const struct map_info *mm);

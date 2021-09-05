#include <stdlib.h>

#include "../common.h"
#include "raster.h"

void raster_free(struct raster_desc *desc) {
	free(desc->palette);
}

size_t raster_stride(const struct raster_desc *desc) {
	return scanline_length(desc->w * desc->ch, desc->bitdepth,
		desc->alignment);
}

size_t raster_size(const struct raster_desc *desc) {
	return raster_stride(desc) * desc->h;
}

void raster_normalize(struct raster_desc *desc) {
	if (!desc->alignment) {
		desc->alignment = 1;
	}
	if (!desc->layout) {
		if (desc->palette || desc->attr == pix_packing_332) {
			desc->layout = pix_rgba;
		} else {
			switch (desc->ch) {
			case 1: desc->layout = pix_gray; break;
			case 2: desc->layout = pix_gray_alpha; break;
			default: desc->layout = pix_rgba; break;
			}
		}
	}
}

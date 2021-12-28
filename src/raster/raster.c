#include <stdlib.h>

#include "../common.h"
#include "raster.h"

void raster_free(struct raster_desc *desc) {
	free(desc->palette);
}

size_t raster_stride(const struct raster_desc *desc) {
	size_t bd = desc->bitdepth;
	if (!desc->planar) {
		bd *= desc->ch;
	}
	return scanline_length(desc->w, bd, desc->alignment);
}

size_t raster_size(const struct raster_desc *desc) {
	size_t size = raster_stride(desc) * desc->h;
	if (desc->planar) {
		size *= desc->ch;
	}
	return size;
}

const char * raster_geom_verify(const struct raster_pal *palette,
const uint8_t ch, const uint8_t bitdepth, const enum pix_attr attr) {
	if (palette) {
		if (ch != 1) {
			return "Paletted images must use 1 channel only";
		} else if (bitdepth > 8) {
			return "Paletted images must not use more than 8 bits";
		} else {
			switch (attr) {
			case pix_normal:
			case pix_inverted:
				break;
			case pix_float:
				return "Paletted images can't use floats";
			case pix_packing_332:
				return "Paletted images can't use 332 packing";
			case pix_packing_1555:
				return "Paletted images can't use 1555 packing";
			default:
				return "Undefined pixel attribute in paletted image";
			}
		}
	} else {
		if (ch == 0) {
			return "Channel number must not be zero";
		} else if (bitdepth == 0) {
			return "Bitdepth must not be zero";
		}

		const int depth = ch * bitdepth;
		switch (attr) {
		case pix_normal:
		case pix_inverted:
		case pix_float:
			break;
		case pix_packing_332:
			if (depth != 8) {
				return "pix_packing_332 must be set with 1"
					"channel and 8 bits";
			}
			break;
		case pix_packing_1555:
			if (depth != 16) {
				return "pix_packing_1555 must be set with 1"
					"channel and 16 bits";
			}
			break;
		default:
			return "Undefined pixel attribute";
		}
	}
	return NULL;
}

void raster_normalize(struct raster_desc *desc) {
	const char *err_msg = raster_geom_verify(desc->palette, desc->ch,
		desc->bitdepth, desc->attr);
	if (err_msg) {
		fatal_bug("Bad raster", err_msg);
	}

	switch (desc->attr) {
	case pix_normal:
	case pix_inverted:
	case pix_float:
		break;
	case pix_packing_332:
		desc->ch = 1;
		desc->bitdepth = 8;
		break;
	case pix_packing_1555:
		desc->ch = 1;
		desc->bitdepth = 16;
		break;
	}

	if (desc->ch == 1) {
		desc->planar = false;
	}

	if (!desc->alignment) {
		desc->alignment = 1;
	}

	if (!desc->layout) {
		if (desc->palette || desc->attr == pix_packing_332) {
			desc->layout = pix_rgba;
		} else {
			if (desc->ch >= 3) {
				desc->layout = pix_rgba;
			} else {
				desc->layout = pix_gray;
			}
		}
	}
}

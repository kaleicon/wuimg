#include <stdlib.h>

#include "../common.h"
#include "raster.h"

size_t raster_size(const struct raster_desc *desc) {
	return scanline_length(desc->w * desc->ch, desc->bitdepth, 0)
		* desc->h;
}

const char * raster_geom_verify(const uint8_t ch, const uint8_t bitdepth,
const enum pix_attr attr, const bool paletted) {
	if (!ch) {
		return "Channel number must not be zero";
	} else if (!bitdepth) {
		return "Bitdepth must not be zero";
	}

	if (paletted) {
		if (ch != 1) {
			return "Paletted images must use 1 channel";
		} else if (bitdepth > 8) {
			return "Paletted images must not use more than 8 bits";
		} else {
			switch (attr) {
			case pix_normal:
			case pix_inverted:
				break;
			case pix_signed:
				return "Paletted images can't use signed indices";
			case pix_float:
				return "Paletted images can't use floats";
			case pix_pack_332:
				return "Paletted images can't use 332 packing";
			case pix_pack_1555:
				return "Paletted images can't use 1555 packing";
			default:
				return "Undefined pixel attribute in paletted image";
			}
		}
	} else {
		const int depth = ch * bitdepth;
		switch (attr) {
		case pix_normal:
		case pix_signed:
		case pix_inverted:
		case pix_float:
			break;
		case pix_pack_332:
			if (depth != 8) {
				return "pix_pack_332 must be set with 1"
					"channel and 8 bits";
			}
			break;
		case pix_pack_1555:
			if (depth != 16) {
				return "pix_pack_1555 must be set with 1"
					"channel and 16 bits";
			}
			break;
		default:
			return "Undefined pixel attribute";
		}
	}
	return NULL;
}

bool raster_test_overflow(size_t w, const size_t h, const uint8_t ch,
const uint8_t bitdepth, const align_t align) {
	if (w < 1 || h < 1) {
		return false;
	}
	if (SIZE_MAX / w / ch == 0) {
		return false;
	}
	w *= ch;

	if (SIZE_MAX / w / bitdepth == 0) {
		return false;
	}

	size_t scanline = scanline_length(w, bitdepth, 0);
	const size_t a = ~0lu << align;
	if (SIZE_MAX - ~a < scanline) {
		return false;
	}
	scanline = (scanline + ~a) & a;
	return SIZE_MAX / h / scanline != 0;
}

bool raster_normalize(struct raster_desc *desc) {
	const char *err_msg = raster_geom_verify(desc->ch, desc->bitdepth,
		desc->attr, false);
	if (err_msg) {
		fatal_bug("Bad raster", err_msg);
	}

	switch (desc->attr) {
	case pix_normal:
	case pix_signed:
	case pix_inverted:
	case pix_float:
		break;
	case pix_pack_332:
		desc->ch = 1;
		desc->bitdepth = 8;
		break;
	case pix_pack_1555:
		desc->ch = 1;
		desc->bitdepth = 16;
		break;
	}

	if (!desc->layout) {
		if (desc->attr == pix_pack_332 || desc->ch >= 3) {
			desc->layout = pix_rgba;
		} else {
			desc->layout = pix_gray;
		}
	}
	return raster_test_overflow(desc->w, desc->h, desc->ch, desc->bitdepth,
		0);
}

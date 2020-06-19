//#include <stddef.h>
#include <string.h>

#include "common_unpack.h"

size_t scanline_length(const size_t width, const unsigned char bitdepth,
const unsigned char boundary) {
	const size_t bytes = (width * bitdepth + 7) / 8;
	return (bytes + boundary - 1) / boundary * boundary;
}

// Unpack n-bit onto unsigned chars

static void unpack4(const unsigned char byte, unsigned char *data,
const size_t nr) {
	switch (nr) {
	case 2:
		data[1] = byte & 0x0f;
		// Fallthrough
	case 1:
		data[0] = byte >> 4;
	}
}

static void unpack2(const unsigned char byte, unsigned char *data,
const size_t nr) {
	switch (nr) {
	case 4:
		data[3] = byte & 0x03;
		// Fallthrough
	case 3:
		data[2] = (byte >> 2) & 0x03;
		// Fallthrough
	case 2:
		data[1] = (byte >> 4) & 0x03;
		// Fallthrough
	case 1:
		data[0] = byte >> 6;
	}
}

static void unpack1(const unsigned char byte, unsigned char *data,
const size_t nr) {
	for (size_t i = 0; i < nr; ++i) {
		data[i] = (byte >> (7 - i)) & 0x01;
	}
}

// Expand n-bit to 8-bit

static void expand_invert4(const unsigned char byte, unsigned char *data,
const size_t nr) {
	const int s = 0xff / 0x0f;
	switch (nr) {
	case 2:
		data[1] = (unsigned char)~( byte * s );
		// Fallthrough
	case 1:
		data[0] = (unsigned char)~( (byte >> 4) * s );
	}
}

static void expand4(const unsigned char byte, unsigned char *data,
const size_t nr) {
	const int s = 0xff / 0x0f;
	switch (nr) {
	case 2:
		data[1] = (unsigned char)( (byte & 0x0f) * s );
		// Fallthrough
	case 1:
		data[0] = (unsigned char)( (byte >> 4) * s);
	}
}

static void expand_invert2(const unsigned char byte, unsigned char *data,
const size_t nr) {
	const int s = 0xff / 0x03;
	switch (nr) {
	case 4:
		data[3] = (unsigned char)~( (byte & 0x03) * s );
		// Fallthrough
	case 3:
		data[2] = (unsigned char)~( ((byte >> 2) & 0x03) * s );
		// Fallthrough
	case 2:
		data[1] = (unsigned char)~( ((byte >> 4) & 0x03) * s );
		// Fallthrough
	case 1:
		data[0] = (unsigned char)~( (byte >> 6) * s );
		// Fallthrough
	}
}

static void expand2(const unsigned char byte, unsigned char *data,
const size_t nr) {
	const int s = 0xff / 0x03;
	switch (nr) {
	case 4:
		data[3] = (unsigned char)( (byte & 0x03) * s );
		// Fallthrough
	case 3:
		data[2] = (unsigned char)( ((byte >> 2) & 0x03) * s );
		// Fallthrough
	case 2:
		data[1] = (unsigned char)( ((byte >> 4) & 0x03) * s );
		// Fallthrough
	case 1:
		data[0] = (unsigned char)( (byte >> 6) * s );
		// Fallthrough
	}
}

static void expand_invert1(const unsigned char byte, unsigned char *data,
const size_t nr) {
	for (size_t i = 0; i < nr; ++i) {
		data[i] = (byte & (0x80 >> i)) ? 0x00 : 0xff;
	}
}

static void expand1(const unsigned char byte, unsigned char *data,
const size_t nr) {
	for (size_t i = 0; i < nr; ++i) {
		data[i] = (byte & (0x80 >> i)) ? 0xff : 0x00;
	}
}

// Have the compiler inline one of the above

static inline void select_unpack(const unsigned char byte, unsigned char *data,
const size_t nr, const unsigned char bitdepth, const enum unpack_op action) {
	switch (action) {
	case unpack:
		switch (bitdepth) {
		case 1: unpack1(byte, data, nr); break;
		case 2: unpack2(byte, data, nr); break;
		case 4: unpack4(byte, data, nr); break;
		}
		break;
	case expand:
		switch (bitdepth) {
		case 1: expand1(byte, data, nr); break;
		case 2: expand2(byte, data, nr); break;
		case 4: expand4(byte, data, nr); break;
		}
		break;
	case expand_invert:
		switch (bitdepth) {
		case 1: expand_invert1(byte, data, nr); break;
		case 2: expand_invert2(byte, data, nr); break;
		case 4: expand_invert4(byte, data, nr); break;
		}
		break;
	default:
		break;
	}
}

// Oddball out

void strip_invert8(unsigned char *data, const size_t len) {
	for (size_t i = 0; i < len; ++i) {
		data[i] = (unsigned char)~data[i];
	}
}

// The above but looping.

static inline unsigned char * strip_common(unsigned char *restrict data,
unsigned char *restrict buf, const size_t width, const size_t height,
const unsigned char boundary, const unsigned char bitdepth,
const enum unpack_op action) {
	const size_t biab = 8U / bitdepth;

	const size_t remainer = width % biab;
	const size_t rowpad = scanline_length(width, bitdepth, boundary)
		- width / biab;
	for (size_t i = 0; i < height; ++i) {
		for (size_t j = 0; j < width / biab; ++j) {
			select_unpack(*buf, data, biab, bitdepth, action);
			data += biab;
			++buf;
		}
		select_unpack(*buf, data, remainer, bitdepth, action);
		data += remainer;
		buf += rowpad;
	}
	return data;
}

unsigned char * strip_invert4(unsigned char *restrict data,
unsigned char *restrict buf, const size_t width, const size_t height,
const unsigned char boundary) {
	return strip_common(data, buf, width, height, boundary, 4, expand_invert);
}

unsigned char * strip_invert2(unsigned char *restrict data,
unsigned char *restrict buf, const size_t width, const size_t height,
const unsigned char boundary) {
	return strip_common(data, buf, width, height, boundary, 2, expand_invert);
}

unsigned char * strip_invert1(unsigned char *restrict data,
unsigned char *restrict buf, const size_t width, const size_t height,
const unsigned char boundary) {
	return strip_common(data, buf, width, height, boundary, 1, expand_invert);
}

unsigned char * strip_expand4(unsigned char *restrict data,
unsigned char *restrict buf, const size_t width, const size_t height,
const unsigned char boundary) {
	return strip_common(data, buf, width, height, boundary, 4, expand);
}

unsigned char * strip_expand2(unsigned char *restrict data,
unsigned char *restrict buf, const size_t width, const size_t height,
const unsigned char boundary) {
	return strip_common(data, buf, width, height, boundary, 2, expand);
}

unsigned char * strip_expand1(unsigned char *restrict data,
unsigned char *restrict buf, const size_t width, const size_t height,
const unsigned char boundary) {
	return strip_common(data, buf, width, height, boundary, 1, expand);
}

unsigned char * strip_unpack4(unsigned char *restrict data,
unsigned char *restrict buf, const size_t width, const size_t height,
const unsigned char boundary) {
	return strip_common(data, buf, width, height, boundary, 4, unpack);
}

unsigned char * strip_unpack2(unsigned char *restrict data,
unsigned char *restrict buf, const size_t width, const size_t height,
const unsigned char boundary) {
	return strip_common(data, buf, width, height, boundary, 2, unpack);
}

unsigned char * strip_unpack1(unsigned char *restrict data,
unsigned char *restrict buf, const size_t width, const size_t height,
const unsigned char boundary) {
	return strip_common(data, buf, width, height, boundary, 1, unpack);
}

unsigned char * strip_unpack(unsigned char *restrict data,
unsigned char *restrict buf, const size_t width, const size_t height,
const unsigned char boundary, const unsigned char bitdepth,
const enum unpack_op op) {
	switch (op) {
	case unpack:
		switch (bitdepth) {
		case 1: return strip_unpack1(data, buf, width, height, boundary);
		case 2: return strip_unpack2(data, buf, width, height, boundary);
		case 4: return strip_unpack4(data, buf, width, height, boundary);
		}
		break;
	case expand:
		switch (bitdepth) {
		case 1: return strip_expand1(data, buf, width, height, boundary);
		case 2: return strip_expand2(data, buf, width, height, boundary);
		case 4: return strip_expand4(data, buf, width, height, boundary);
		}
		break;
	case expand_invert:
		switch (bitdepth) {
		case 1: return strip_invert1(data, buf, width, height, boundary);
		case 2: return strip_invert2(data, buf, width, height, boundary);
		case 4: return strip_invert4(data, buf, width, height, boundary);
		}
		break;
	default:
		break;
	}
	return NULL;
}

// Now with color maps
static unsigned char * expand_rgb_colormap(const unsigned char byte,
unsigned char *restrict data, const struct colormap *cm, const size_t nr,
const unsigned char bitdepth, const unsigned char ch) {
	const unsigned char mask = 0xff ^ (unsigned char)(0xff << bitdepth);
	size_t i = 8;
	size_t bound = i - nr * bitdepth;
	while (i > bound) {
		i -= bitdepth;
		const int idx = (byte >> i) & mask;
		memcpy(data, cm + idx, ch);
		data += ch;
	}
	return data;
}

static inline unsigned char * colormap_common(unsigned char *restrict data,
unsigned char *restrict buf, const void *cm, const size_t width,
const size_t height, const unsigned char bitdepth, const unsigned char ch,
const unsigned char boundary) {
	const size_t biab = 8U / bitdepth;

	const size_t remainer = width % biab;
	const size_t rowpad = scanline_length(width, bitdepth, boundary) - width / biab;
	for (size_t i = 0; i < height; ++i) {
		for (size_t j = 0; j < width / biab; ++j) {
			data = expand_rgb_colormap(*buf, data, cm, biab, bitdepth, ch);
			++buf;
		}
		data = expand_rgb_colormap(*buf, data, cm, remainer, bitdepth, ch);
		buf += rowpad;
	}
	return data;
}

unsigned char * strip_colormap_rgb8(unsigned char *restrict data,
unsigned char *restrict buf, const void *cm, const size_t width,
const size_t height, const unsigned char boundary) {
	return colormap_common(data, buf, cm, width, height, 8, 3, boundary);
}

unsigned char * strip_colormap_rgb4(unsigned char *restrict data,
unsigned char *restrict buf, const void *cm, const size_t width,
const size_t height, const unsigned char boundary) {
	return colormap_common(data, buf, cm, width, height, 4, 3, boundary);
}

unsigned char * strip_colormap_rgb2(unsigned char *restrict data,
unsigned char *restrict buf, const void *cm, const size_t width,
const size_t height, const unsigned char boundary) {
	return colormap_common(data, buf, cm, width, height, 2, 3, boundary);
}

unsigned char * strip_colormap_rgb1(unsigned char *restrict data,
unsigned char *restrict buf, const void *cm, const size_t width,
const size_t height, const unsigned char boundary) {
	return colormap_common(data, buf, cm, width, height, 1, 3, boundary);
}

unsigned char * strip_colormap_rgba8(unsigned char *restrict data,
unsigned char *restrict buf, const void *cm, const size_t width,
const size_t height, const unsigned char boundary) {
	return colormap_common(data, buf, cm, width, height, 8, 4, boundary);
}

unsigned char * strip_colormap_rgba4(unsigned char *restrict data,
unsigned char *restrict buf, const void *cm, const size_t width,
const size_t height, const unsigned char boundary) {
	return colormap_common(data, buf, cm, width, height, 4, 4, boundary);
}

unsigned char * strip_colormap_rgba2(unsigned char *restrict data,
unsigned char *restrict buf, const void *cm, const size_t width,
const size_t height, const unsigned char boundary) {
	return colormap_common(data, buf, cm, width, height, 2, 4, boundary);
}

unsigned char * strip_colormap_rgba1(unsigned char *restrict data,
unsigned char *restrict buf, const void *cm, const size_t width,
const size_t height, const unsigned char boundary) {
	return colormap_common(data, buf, cm, width, height, 1, 4, boundary);
}

unsigned char * strip_colormap(unsigned char *restrict data,
unsigned char *restrict buf, const void *cm, const size_t width,
const size_t height, const unsigned char boundary, const unsigned char bitdepth,
const unsigned char channels) {
	switch (channels) {
	case 3:
		switch (bitdepth) {
		case 1: return strip_colormap_rgb1(data, buf, cm, width, height, boundary);
		case 2: return strip_colormap_rgb2(data, buf, cm, width, height, boundary);
		case 4: return strip_colormap_rgb4(data, buf, cm, width, height, boundary);
		case 8: return strip_colormap_rgb8(data, buf, cm, width, height, boundary);
		}
		break;
	case 4:
		switch (bitdepth) {
		case 1: return strip_colormap_rgba1(data, buf, cm, width, height, boundary);
		case 2: return strip_colormap_rgba2(data, buf, cm, width, height, boundary);
		case 4: return strip_colormap_rgba4(data, buf, cm, width, height, boundary);
		case 8: return strip_colormap_rgba8(data, buf, cm, width, height, boundary);
		}
		break;
	}
	return NULL;
}

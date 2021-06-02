#include "unpack.h"

// Can't be bothered to write a table
struct colormap lib_ega_palette(const size_t idx) {
	const size_t r = ((idx >> 1) & 2) | ((idx >> 5) & 1);
	const size_t g = ((idx     ) & 2) | ((idx >> 4) & 1);
	const size_t b = ((idx << 1) & 2) | ((idx >> 3) & 1);
	return (struct colormap) {
		.r = (unsigned char)(r * 0x55),
		.g = (unsigned char)(g * 0x55),
		.b = (unsigned char)(b * 0x55),
		.a = 0xff,
	};
}

struct colormap lib_cga_palette(const size_t idx) {
	const size_t bright = idx >> 3;
	const size_t r = ((idx >> 1) & 2) | bright;
	const size_t g = ((idx     ) & 2) | bright;
	const size_t b = ((idx << 1) & 2) | bright;

	const size_t orange = idx == 6 ? 0x55 : 0;
	return (struct colormap) {
		.r = (unsigned char)(r * 0x55),
		.g = (unsigned char)(g * 0x55 - orange),
		.b = (unsigned char)(b * 0x55),
		.a = 0xff,
	};
}

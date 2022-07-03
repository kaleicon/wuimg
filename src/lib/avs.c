#include "avs.h"

// I like this format.

enum wu_error avs_open_file(struct raw_img *img, FILE *ifp) {
	uint32_t buf[2];
	if (fread(buf, sizeof(buf), 1, ifp)) {
		*img = (struct raw_img) {
			.w = endian32(buf[0], big_endian),
			.h = endian32(buf[1], big_endian),
			.channels = 4,
			.bitdepth = 8,
			.layout = pix_argb,
		};
		return wu_ok;
	}
	return wu_unexpected_eof;
}

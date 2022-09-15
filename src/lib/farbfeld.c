#include "raster/fmt.h"
#include "farbfeld.h"

enum wu_error farbfeld_open_file(struct raw_img *img, FILE *ifp) {
	const uint8_t magic[8] = "farbfeld";
	const enum wu_error st = fmt_sigcmp(magic, sizeof(magic), ifp);
	if (st == wu_ok) {
		uint32_t buf[2];
		if (fread(buf, sizeof(buf), 1, ifp)) {
			img->w = endian32(buf[0], big_endian);
			img->h = endian32(buf[1], big_endian);
			img->channels = 4;
			img->bitdepth = 16;
			return wu_ok;
		}
		return wu_unexpected_eof;
	}
	return st;
}

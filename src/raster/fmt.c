#include <stdlib.h>
#include <string.h>

#include "raster/fmt.h"

enum wu_error fmt_load_pal_planar(FILE *ifp, struct raster_pal *pal,
const enum fmt_pal_type type, const size_t entries) {
	enum wu_error err = wu_alloc_error;
	const size_t planes = (size_t)type;
	unsigned char *buf = malloc(entries * planes);
	if (buf) {
		if (fread(buf, entries * planes, 1, ifp)) {
			for (size_t i = 0; i < entries; ++i) {
				pal->color[i].r = buf[i];
				pal->color[i].g = buf[i + entries];
				pal->color[i].b = buf[i + entries*2];
				pal->color[i].a = 0xff;
			}
			err = wu_ok;
		} else {
			err = wu_unexpected_eof;
		}
		free(buf);
	}
	return err;
}

enum wu_error fmt_load_pal(FILE *ifp, struct raster_pal *pal,
const enum fmt_pal_type type, const size_t entries) {
	const size_t elen = (size_t)type;
	unsigned char *buf = (unsigned char *)pal + (4 - elen) * entries;
	if (fread(buf, elen * entries, 1, ifp)) {
		if (elen == 3) {
			raster_pal_from_rgb8(pal, buf, entries);
		}
		return wu_ok;
	}
	return wu_unexpected_eof;
}

enum wu_error fmt_sigcmp_mem(const unsigned char *restrict sig,
const size_t size, struct mp_parser *mp) {
	const uint8_t *buf = mp_next_slice(mp, size);
	if (buf) {
		return !memcmp(buf, sig, size) ? wu_ok : wu_invalid_signature;
	}
	return wu_unexpected_eof;
}

enum wu_error fmt_sigcmp(const unsigned char *restrict sig, const size_t size,
FILE *ifp) {
	unsigned char buf[8];
	for (size_t off = 0; off < size; off += sizeof(buf)) {
		const size_t len = zumin(size - off, sizeof(buf));
		if (!fread(buf, len, 1, ifp)) {
			return wu_unexpected_eof;
		}
		if (memcmp(buf, sig + off, len)) {
			return wu_invalid_signature;
		}
	}
	return wu_ok;
}

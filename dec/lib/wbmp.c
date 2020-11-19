#include <stdio.h>
#include <stdlib.h>

#include "../../common.h"
#include "common/lib.h"
#include "common/unpack.h"
#include "wbmp.h"

unsigned char * wbmp_decode(struct wbmp_desc *desc, const bool expand_bitmap) {
	const enum unpack_op op = expand_bitmap ? expand : noop;
	return strip_map_unpack(desc->ifp, desc->w, desc->h, 1, op, 1);
}

static enum lib_fail check_filesize(const size_t expected_size, FILE *ifp) {
	const long begin = ftell(ifp);
	fseek(ifp, 0, SEEK_END);
	const long end = ftell(ifp);
	if ((size_t)(end - begin) >= expected_size) {
		fseek(ifp, begin, SEEK_SET);
		return lib_ok;
	}
	return lib_invalid_header;
}

static size_t read_uintvar_dim(FILE *ifp) {
	size_t value = 0;
	for (size_t i = 0; i < sizeof(value); ++i) {
		const int c = getc(ifp);
		if (c == EOF) {
			return 0;
		} else {
			value = (value << 7) | ((unsigned int)c & 0x7f);
			if (c < 0x80) {
				return value;
			}
		}
	}
	return 0;
}

enum lib_fail wbmp_open_file(FILE *ifp, struct wbmp_desc *desc) {
	unsigned char buf[2];
	if (fread(buf, 1, sizeof(buf), ifp)) {
		if (memchk(buf, 0, sizeof(buf))) {
			desc->w = read_uintvar_dim(ifp);
			desc->h = read_uintvar_dim(ifp);
			if (desc->w && desc->h) {
				desc->ifp = ifp;
				const size_t expected_size = ((desc->w + 7) / 8)
					* desc->h;
				return check_filesize(expected_size, ifp);
			}
		}
		return lib_invalid_header;
	}
	return lib_unexpected_eof;
}

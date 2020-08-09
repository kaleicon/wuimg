#include <stdio.h>
#include <stdlib.h>

#include "common.h"
#include "common_lib.h"
#include "common_unpack.h"
#include "lib_wbmp.h"

unsigned char * wbmp_decode(struct wbmp_desc *desc) {
	const size_t dims = desc->w * desc->h;
	unsigned char *output = malloc(dims);
	if (!output) {
		return NULL;
	}

	const size_t data_size = scanline_length(desc->w, 1, 1) * desc->h;
	unsigned char *outbits = output + dims - data_size;

	const size_t read = fread(outbits, 1, data_size, desc->ifp);
	if (read != data_size) {
		free(output);
		return NULL;
	}

	strip_unpack(output, outbits, desc->w, desc->h, 1, expand, 1);
	return output;
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
	if (getc(ifp) == 0 && getc(ifp) == 0) {
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

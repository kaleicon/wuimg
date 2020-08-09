#include <stdio.h>
#include <stdlib.h>

#include "common.h"

#include "common_lib.h"

u_int8_t * avs_load(FILE *ifp, const size_t width, const size_t height) {
	const size_t dims = width * height;
	u_int8_t *data = malloc(dims * 4);
	if (data) {
		const size_t read = fread(data, 4, dims, ifp);
		if (read < dims) {
			puts(RASTER_EOF);
		}
	}
	return data;
}

enum lib_fail avs_open_file(FILE *ifp, size_t *width, size_t *height) {
	u_int32_t buf[2];
	if (fread(buf, 1, sizeof(buf), ifp) == sizeof(buf)) {
		*width = endian32(buf[0], big_endian);
		*height = endian32(buf[1], big_endian);
		return lib_ok;
	}
	return lib_unexpected_eof;
}

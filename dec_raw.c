#include <libraw/libraw.h>

#include "wudefs.h"
#include "common.h"

enum wu_error raw_dec(struct image_file *infile, const struct wu_conf *wuconf) {
	libraw_data_t *data = libraw_init(0);
	if (!data) {
		return wu_alloc_error;
	}

	size_t size = 0;
	unsigned char *buf = read_file_to_mem(desc->ifp, &size);
	if (!buf) {
		libraw_close(data);
		return wu_alloc_error;
	}

	if (libraw_open_buffer(data, buf, size)) {
		libraw_close(data);
		return wu_open_error;
	}



	libraw_close(data);
	return wu_ok;
}

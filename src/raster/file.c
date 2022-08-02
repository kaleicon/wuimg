#include <stdlib.h>

#include "common.h"
#include "raster/file.h"

uint8_t * fileccpy(struct wugrow *grow, const uint8_t ch, const size_t max,
FILE *ifp) {
	uint8_t *buf = NULL;
	*grow = wugrow_init(sizeof(*buf));
	while (grow->pos < max) {
		const int c = getc(ifp);
		if (c == EOF) {
			break;
		} else if (c == ch) {
			return buf;
		}
		if (!wugrow_recheck(&buf, grow)) {
			break;
		}
		buf[grow->pos] = (uint8_t)c;
		++grow->pos;
	}
	free(buf);
	return NULL;
}

size_t fread_tail(void *buf, const size_t size, const size_t nmemb,
FILE *ifp) {
	const size_t total = size*nmemb;
	return fseek(ifp, -(long)total, SEEK_END) == 0
		? fread(buf, size, nmemb, ifp) : 0;
}

long file_size(FILE *ifp) {
	fseek(ifp, 0, SEEK_END);
	return ftell(ifp);
}

size_t file_size_from(FILE *ifp, const long start) {
	return (size_t)lmax(0, file_size(ifp) - start);
}

long file_remaining(FILE *ifp) {
	const long cur = ftell(ifp);
	const long end = file_size(ifp);
	fseek(ifp, cur, SEEK_SET);
	return lmax(0, end - cur);
}

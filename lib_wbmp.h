#ifndef LIB_WBMP
#define LIB_WBMP

#include <stdio.h>

enum wbmp_fail {
	wbmp_ok = 0,
	wbmp_open_error,
	wbmp_invalid_header,
	wbmp_invalid_file
};

struct wbmp_desc {
	size_t w, h;
	FILE *ifp;
};

const char * wbmp_fail_string(const enum wbmp_fail fail);

unsigned char * wbmp_decode(struct wbmp_desc *desc);

enum wbmp_fail wbmp_open_file(FILE *ifp, struct wbmp_desc *desc);

#endif /* LIB_WBMP */

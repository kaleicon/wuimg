#ifndef LIB_WBMP
#define LIB_WBMP

#include <stdio.h>

#include "common/lib.h"

struct wbmp_desc {
	FILE *ifp;
	size_t w, h;
};

unsigned char * wbmp_decode(const struct wbmp_desc *desc, bool expand_bitmap);

enum lib_fail wbmp_open_file(FILE *ifp, struct wbmp_desc *desc);

#endif /* LIB_WBMP */

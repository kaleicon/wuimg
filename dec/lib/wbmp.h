#ifndef LIB_WBMP
#define LIB_WBMP

#include <stdio.h>

#include "common/lib.h"

struct wbmp_desc {
	size_t w, h;
	FILE *ifp;
};

unsigned char * wbmp_decode(struct wbmp_desc *desc, const bool expand_bitmap);

enum lib_fail wbmp_open_file(FILE *ifp, struct wbmp_desc *desc);

#endif /* LIB_WBMP */

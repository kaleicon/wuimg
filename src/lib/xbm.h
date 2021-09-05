#ifndef LIB_XBM
#define LIB_XBM

#include <stdio.h>
#include <stdbool.h>

#include "../raster/lib.h"

enum xbm_type {
	xbm_x11,
	xbm_x10,
};

struct xbm_desc {
	FILE *ifp;
	char *name;
	size_t name_len;

	unsigned int w, h;
	int x_hot, y_hot;
	enum xbm_type type;
	bool has_hotspot;
};

void xbm_cleanup(struct xbm_desc *desc);

unsigned char * xbm_decode(const struct xbm_desc *desc);

enum lib_fail xbm_read_header(struct xbm_desc *desc);

enum lib_fail xbm_open_file(FILE *ifp, struct xbm_desc *desc);

#endif /* LIB_XBM */

#ifndef LIB_XBM
#define LIB_XBM

#include <stdio.h>
#include <stdbool.h>

#include "../wustr.h"
#include "../raster/lib.h"
#include "../raster/text.h"

enum xbm_type {
	xbm_x10,
	xbm_x11,
};

struct xbm_desc {
	struct raster_desc r;

	unsigned int x_hot, y_hot;
	bool has_hotspot;
	enum xbm_type type;

	struct text_parser tp;

	struct wustr name;
	struct wustr comment;
};

void xbm_cleanup(struct xbm_desc *desc);

unsigned char * xbm_decode(const struct xbm_desc *desc);

enum lib_fail xbm_open_file(struct xbm_desc *desc, FILE *ifp);

#endif /* LIB_XBM */

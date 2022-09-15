#ifndef LIB_XBM
#define LIB_XBM

#include <stdbool.h>

#include "wudefs.h"
#include "common/wustr.h"
#include "raster/memparser.h"

enum xbm_type {
	xbm_x11 = 1,
	xbm_x10 = 2,
};

struct xbm_desc {
	struct mp_parser tp;

	long x_hot, y_hot;
	bool has_hotspot;
	enum xbm_type type;

	struct wuptr name;
	struct wuptr comment;
};

size_t xbm_decode(const struct xbm_desc *desc, struct raw_img *img);

enum wu_error xbm_parse_header(struct xbm_desc *desc, struct raw_img *img,
struct mp_parser mp);

#endif /* LIB_XBM */

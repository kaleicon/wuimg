#ifndef LIB_XBM
#define LIB_XBM

#include <stdbool.h>

#include "../wustr.h"
#include "../raster/lib.h"
#include "../raster/text.h"

enum xbm_type {
	xbm_x11 = 1,
	xbm_x10 = 2,
};

struct xbm_desc {
	struct text_parser tp;
	struct raster_desc r;

	unsigned int x_hot, y_hot;
	bool has_hotspot;
	enum xbm_type type;

	struct wuptr name;
	struct wuptr comment;
};

size_t xbm_decode(const struct xbm_desc *desc, void *restrict dst);

enum lib_fail xbm_open_mem(struct xbm_desc *desc, const struct map_info *mm);

#endif /* LIB_XBM */

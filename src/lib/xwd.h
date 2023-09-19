// SPDX-License-Identifier: 0BSD
#ifndef LIB_XWD
#define LIB_XWD

#include "misc/endian.h"
#include "raster/wuimg.h"

enum xwd_version {
	xwd_x10 = 6,
	xwd_x11 = 7,
};

enum xwd_format {
	xwd_xybitmap = 0,
	xwd_xypixmap = 1,
	xwd_zpixmap = 2
};

enum xwd_visual_class {
	xwd_static_gray = 0,
	xwd_gray_scale = 1,
	xwd_static_color = 2,
	xwd_pseudo_color = 3,
	xwd_true_color = 4,
	xwd_direct_color = 5,
};

struct xwd_window {
	uint32_t w, h;
	uint32_t x, y;
	uint32_t border_w;
	struct wustr name;
};

struct xwd_desc {
	FILE *ifp;
	struct xwd_window win;
	enum xwd_version version:8;
	enum xwd_format format:8;
	enum endianness byte_endian:8;
	enum endianness bit_endian:8;
	enum xwd_visual_class visual:8;
};

const char * xwd_version_str(enum xwd_version version);

const char * xwd_format_str(enum xwd_format format);

const char * xwd_visual_str(enum xwd_visual_class visual);

void xwd_cleanup(struct xwd_desc *desc);

size_t xwd_decode(const struct xwd_desc *desc, struct wuimg *img);

enum wu_error xwd_parse(struct xwd_desc *desc, struct wuimg *img);

enum wu_error xwd_open(struct xwd_desc *desc, FILE *ifp);

#endif /* LIB_XWD */

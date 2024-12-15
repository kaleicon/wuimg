// SPDX-License-Identifier: 0BSD
#ifndef LIB_C64
#define LIB_C64

#include "misc/mparser.h"
#include "raster/wuimg.h"

enum c64_fmt {
	c64_koa = 10003,
	c64_ocp = 10018,
	c64_koa_compressed = -1,
};

struct c64_desc {
	struct mparser mp;
	enum c64_fmt fmt;
};

const char * c64_fmt_str(enum c64_fmt fmt);

bool c64_decode(const struct c64_desc *desc, struct wuimg *img);

enum wu_error c64_set(struct wuimg *img);

enum wu_error c64_guess(struct c64_desc *desc, struct wuptr mem);

#endif /* LIB_C64 */

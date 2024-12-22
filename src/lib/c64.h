// SPDX-License-Identifier: 0BSD
#ifndef LIB_C64
#define LIB_C64

#include "misc/mparser.h"
#include "raster/wuimg.h"

enum c64_mode {
	c64_hires,
	c64_multicolor,
};

enum c64_fmt {
	c64_art_studio,
	c64_advanced_art_studio,
	c64_artist64,
	c64_blazing_paddles,
	c64_doodle,
	c64_hi_eddi,
	c64_image_system_m,
	c64_koalapainter,
	c64_saracen_paint,
	c64_vidcom_64,
};

struct c64_desc {
	struct mparser mp;
	enum c64_fmt fmt:16;
	enum c64_mode mode:8;
	bool compressed;
};

const char * c64_mode_str(const enum c64_mode mode);

const char * c64_fmt_str(enum c64_fmt fmt);

bool c64_decode(const struct c64_desc *desc, struct wuimg *img);

enum wu_error c64_set(const struct c64_desc *desc, struct wuimg *img);

enum wu_error c64_guess(struct c64_desc *desc, struct wuptr mem,
const uint8_t ext[static 4]);

#endif /* LIB_C64 */

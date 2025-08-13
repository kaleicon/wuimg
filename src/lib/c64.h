// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2023 kaleido
#ifndef LIB_C64
#define LIB_C64

#include "misc/mparser.h"
#include "raster/wuimg.h"

enum c64_mode {
	c64_none = 0,
	c64_hires,
	c64_multicolor_nobg,
	c64_multicolor,
};

enum c64_fmt {
	c64_afli_editor,
	c64_art_studio,
	c64_advanced_art_studio,
	c64_artist64,
	c64_blazing_paddles,
	c64_cdu_paint,
	c64_cheese,
	c64_doodle,
	c64_fli_designer,
	c64_hi_eddi,
	c64_hires_fli_crest,
	c64_image_system_m,
	c64_koalapainter,
	c64_picasso_64,
	c64_rainbow_painter,
	c64_saracen_paint,
	c64_vidcom_64,
};

enum c64_field {
	c64_bitmap,
	c64_screen,
	c64_color,
	c64_bg,
};

struct c64_fmt_info {
	enum c64_mode mode:8;
	bool fli;
	uint16_t tbl[4];
};

struct c64_desc {
	struct mparser mp;
	bool compressed;
	enum c64_fmt fmt:8;
	struct c64_fmt_info info;
};

const char * c64_mode_str(const enum c64_mode mode);

const char * c64_fmt_str(enum c64_fmt fmt);

struct wu_st c64_decode(const struct c64_desc *desc, struct wuimg *img);

struct wu_st c64_set(const struct c64_desc *desc, struct wuimg *img);

struct wu_st c64_guess(struct c64_desc *desc, struct wuptr mem,
const uint8_t ext[static 4]);

#endif /* LIB_C64 */

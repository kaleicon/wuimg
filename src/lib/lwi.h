// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2025 kaleido
#ifndef LIB_LWI
#define LIB_LWI

#include "raster/wuimg.h"

enum lwi_field {
	lwi_zero = 0x00,
	lwi_image = 0x01,
	lwi_end = 0x13,
	lwi_tool = 0x15,
	lwi_source = 0x16,
	lwi_author = 0x17,
	lwi_copyright = 0x18,
	lwi_timestamp = 0x19,
};

const char * lwi_field_str(enum lwi_field field);

struct wu_st lwi_decode(struct mparser mp, struct wuimg *img);

struct wu_st lwi_next_field(struct mparser *mp, struct wuimg *img,
enum lwi_field *type, struct wuptr *data);

#endif /* LIB_LWI */

// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2025 kaleido
#ifndef LIB_HEL
#define LIB_HEL

#include "raster/wuimg.h"

struct wu_st hel_render_frame(struct wuptr map, struct wuimg *img, uint32_t frame);

struct wu_st hel_identify(struct wuptr map, struct wuimg *img, unsigned fps);

#endif /* LIB_HEL */

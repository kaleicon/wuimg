// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2025 kaleido
#ifndef LIB_HEL
#define LIB_HEL

#include "raster/wuimg.h"

size_t hel_render_frame(struct wuptr map, struct wuimg *img, uint32_t frame);

enum wu_error hel_identify(struct wuptr map, struct wuimg *img, unsigned fps);

#endif /* LIB_HEL */

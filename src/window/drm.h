// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2021 kaleido
#ifndef DRM_BACKEND
#define DRM_BACKEND

#include "base.h"

const char * drm_init(struct window_context *ctx);

const char * drm_offscreen_init(struct window_offscreen *ctx);

#endif /* DRM_BACKEND */

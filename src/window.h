// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2019 kaleido
#ifndef WU_WINDOW
#define WU_WINDOW

#include "window/base.h"

extern const char WINDOW_ENV_VARIABLES[];

void window_terminate(struct window_context *window);

bool window_draw(struct window_context *window);

void window_fullscreen(struct window_context *window);

void window_poll(struct window_context *window, int msecs);

void window_adapt(struct window_context *window);

void window_set_title(struct window_context *window, const char *title);

void window_postgl_setup(struct window_context *window);

bool window_setup(struct window_context *window);


void window_offscreen_terminate(struct window_offscreen *window);

bool window_offscreen_setup(struct window_offscreen *window);

#endif /* WU_WINDOW */

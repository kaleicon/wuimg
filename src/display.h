// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2020 kaleido
#ifndef WU_DISPLAY
#define WU_DISPLAY

#include "misc/term.h"
#include "window.h"

void display_end(struct window_context *window,
const struct term_restore *tr);

enum wu_error display_loop(struct window_context *window, bool allow_cycle,
bool allow_delete);

bool display_setup(struct window_context *window, struct term_restore *tr);

const char * display_offscreen_image(struct gl_context *gl,
struct image_context *image, const struct wuimg *cur);

const char * display_offscreen_setup(struct window_offscreen *window,
struct gl_reader_context *reader, struct wu_conf *conf);

#endif /* WU_DISPLAY */

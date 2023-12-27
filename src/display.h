// SPDX-License-Identifier: 0BSD
#ifndef WU_DISPLAY
#define WU_DISPLAY

#include "term.h"
#include "window.h"

void display_end(struct window_context *window,
const struct term_restore *tr);

enum wu_error display_loop(struct window_context *window, bool allow_cycle,
bool allow_delete);

bool display_setup(struct window_context *window, struct term_restore *tr);

#endif /* WU_DISPLAY */

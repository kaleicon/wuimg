#ifndef DISPLAY
#define DISPLAY

#include "window.h"
#include "term.h"

void display_end(struct window_context *window,
const struct term_restore *tr);

bool display_loop(struct window_context *window, bool no_cycle);

bool display_setup(struct window_context *window, struct term_restore *tr);

#endif /* DISPLAY */

#ifndef DISPLAY
#define DISPLAY

#include "window.h"
#include "term.h"

void display_end(struct window_control *control,
const struct term_restore *tr);

bool display_loop(struct window_control *control, bool no_cycle);

bool display_setup(struct window_control *control, struct term_restore *tr);

#endif /* DISPLAY */

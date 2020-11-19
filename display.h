#ifndef DISPLAY
#define DISPLAY

#include <termios.h>

#include "wudefs.h"
#include "window.h"

struct term_restore {
	tcflag_t lflag;
	cc_t vmin;
	cc_t vtime;
};

void end_display(const struct term_restore *tr);

bool display_loop(struct image_file *infile, struct window_control *control,
const char *filename, bool no_cycle);

bool setup_display(struct window_control *control, struct term_restore *tr);

#endif /* DISPLAY */

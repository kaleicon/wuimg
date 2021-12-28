#ifndef WU_WINDOW_BASE
#define WU_WINDOW_BASE

#include "../wudefs.h"
#include "../opengl.h"
#include "../events.h"

struct window_cursor_axis {
	float pos;
	float scroll;
};

struct window_cursor {
	struct window_cursor_axis x;
	struct window_cursor_axis y;
	bool pressed;
};

struct window_public {
	struct gl_context gl;
	struct timespec timer;
	struct image_context image;
	struct wu_event event;
	struct wu_keymap held_keys;
};

double window_exec_events(struct window_public *pub);

enum trit window_size_update(struct window_public *pub, unsigned w, unsigned h);

void window_scroll_axis(struct window_cursor_axis *axis, const double offset);

void window_scroll(struct window_cursor *cur, const double x, const double y);

void window_cursor_apply_diff(struct window_cursor *cur,
struct window_public *pub, double x, double y);

#endif /* WU_WINDOW_BASE */

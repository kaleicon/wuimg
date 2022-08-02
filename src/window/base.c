#include <string.h>
#include <ctype.h>
#include <math.h>

#include "base.h"

static unsigned char * window_key_get_map(struct window_keymap *held_keys) {
	return held_keys->map - WINDOW_KEYSTART;
}

void window_key_lift(struct window_keymap *held_keys) {
	memset(held_keys, 0, sizeof(*held_keys));
}

void window_key_add(struct window_keymap *held_keys,
const enum key_action action, int code, const bool shift) {
	held_keys->shift = shift;
	code = toupper(code);
	if (code >= WINDOW_KEYSTART && code < WINDOW_KEYEND) {
		unsigned char *map = window_key_get_map(held_keys);
		if (!map[code] || action == key_release) {
			map[code] = action;
		}
	}
}

enum trit window_size_update(struct window_public *pub, const int w,
const int h) {
	if (w > 0 && h > 0) {
		struct display_dims *fb = &pub->image.conf.fb;
		if (fb->w != w || fb->h != h) {
			fb->w = w;
			fb->h = h;
			gl_viewport(&pub->gl, fb);
			return trit_true;
		}
		return trit_false;
	}
	return trit_what;
}

void window_scroll_axis(struct window_cursor_axis *axis, const double offset) {
	if (fpclassify(offset) == FP_NORMAL) {
		axis->scroll = (float)(axis->scroll + offset);
	}
}

void window_scroll(struct window_cursor *cursor, const double x, const double y) {
	window_scroll_axis(&cursor->x, x);
	window_scroll_axis(&cursor->y, y);
}

void window_cursor_move(struct window_public *pub, const double x,
const double y) {
	struct window_common *win = &pub->win;
	struct wu_state *state = &pub->image.state;
	if (win->pressed) {
		const double zoom = 1 / state->zoom;
		state->x_offset += (float)((x - win->cur.x.pos) * zoom);
		state->y_offset += (float)((y - win->cur.y.pos) * zoom);
		pub->gl.update = gl_update_matrix;
	}
	win->cur.x.pos = (float)x;
	win->cur.y.pos = (float)y;
}

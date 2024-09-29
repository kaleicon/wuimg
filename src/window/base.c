// SPDX-License-Identifier: 0BSD
#include <ctype.h>
#include <string.h>

#include "base.h"

double window_timer_update(struct window_public *pub) {
	const struct timespec start = pub->timer;
	clock_gettime(CLOCK_MONOTONIC, &pub->timer);

	const double nanos_per_sec = 1000000000;
	return (double)(pub->timer.tv_sec - start.tv_sec)
		+ (double)(pub->timer.tv_nsec - start.tv_nsec) / nanos_per_sec;
}

unsigned char * window_keymap_map(struct window_keymap *held_keys) {
	return held_keys->map - WINDOW_KEYSTART;
}

void window_key_lift(struct window_keymap *held_keys) {
	memset(held_keys, 0, sizeof(*held_keys));
}

void window_key_add(struct window_keymap *held_keys,
const enum key_action action, int code, const bool shift) {
	code = toupper(code);
	if (code >= WINDOW_KEYSTART && code < WINDOW_KEYEND) {
		held_keys->shift = shift;
		code -= WINDOW_KEYSTART;
		if (!held_keys->map[code] || action == key_release) {
			held_keys->map[code] = (uint8_t)action;
		}
	}
}

enum trit window_size_update(struct window_public *pub, const int w,
const int h) {
	if (w > 0 && h > 0) {
		struct display_dims *fb = &pub->image.state.fb;
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
	const float off = (float)offset;
	if (isnormal(off)) {
		axis->scroll += off;
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
		const float x_diff = ((float)x - win->cur.x.pos) / state->zoom;
		const float y_diff = ((float)y - win->cur.y.pos) / state->zoom;
		if (isnormal(x_diff) || isnormal(y_diff)) {
			state->x_offset += x_diff;
			state->y_offset += y_diff;
			pub->event.image = ev_move;
		}
	}
	win->cur.x.pos = (float)x;
	win->cur.y.pos = (float)y;
}

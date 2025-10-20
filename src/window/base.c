// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2021 kaleido
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

static enum image_event move_state(struct wu_state *state, float x, float y) {
	const float x_diff = x / state->zoom;
	const float y_diff = y / state->zoom;
	if (isnormal(x_diff) || isnormal(y_diff)) {
		state->x_offset += x_diff;
		state->y_offset += y_diff;
		return ev_transform;
	}
	return ev_none;
}

void window_cursor_scroll(struct window_public *pub, double x, double y) {
	pub->event.image = move_state(&pub->image.state, -(float)x, -(float)y);
}

void window_cursor_move(struct window_public *pub, const double x,
const double y) {
	struct window_common *win = &pub->win;
	if (win->pressed) {
		pub->event.image = move_state(&pub->image.state,
			(float)x - win->cur.x.pos, (float)y - win->cur.y.pos);
	}
	win->cur.x.pos = (float)x;
	win->cur.y.pos = (float)y;
}

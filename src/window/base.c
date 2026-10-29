// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2021 kaleido
#include <ctype.h>
#include <limits.h>
#include <math.h>
#include <string.h>

#include "events.h"
#include "misc/math.h"
#include "misc/time.h"
#include "window/base.h"

uint32_t window_refresh_nsec(const struct window_context *ctx) {
	const uint32_t n = ctx->win.refresh_nsec;
	return n ? n : 1000000000/60;
}

unsigned char * window_keymap_map(struct window_keymap *held_keys) {
	return held_keys->map - WINDOW_KEYSTART;
}

void window_key_lift(struct window_context *ctx) {
	memset(&ctx->win.held_keys, 0, sizeof(ctx->win.held_keys));
}

void window_key_add(struct window_context *ctx, const enum key_action action,
int code, const bool shift) {
	struct window_keymap *held_keys = &ctx->win.held_keys;
	code = toupper(code);
	if (code >= WINDOW_KEYSTART && code < WINDOW_KEYEND) {
		held_keys->shift = shift;
		code -= WINDOW_KEYSTART;
		if (!held_keys->map[code] || action == key_released) {
			const bool prev = held_keys->active;
			held_keys->map[code] = (uint8_t)action;
			held_keys->active |= (bool)action;
			if (!prev & held_keys->active) {
				timespec_present_time(&ctx->win.timer.key);
			}
		}
	}
}

enum trit window_size_update(struct window_context *ctx, const int w,
const int h) {
	if (w > 0 && h > 0) {
		struct display_dims *fb = &ctx->image.state.fb;
		if (fb->w != w || fb->h != h) {
			fb->w = w;
			fb->h = h;
			gl_viewport(&ctx->gl, fb);
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

void window_cursor_scroll(struct window_context *ctx, double x, double y,
const bool horz, const bool zoom) {
	struct wu_event *ev = &ctx->event;
	struct wu_state *state = &ctx->image.state;
	if (zoom) {
		const float old_zoom = state->zoom;
		ev->image = wudec_zoom(&ctx->image,
			state->zoom * exp2f((float)y * (-1.0f/32)));
		struct window_common *win = &ctx->win;
		struct display_dims *fb = &ctx->image.state.fb;
		const float mul = 1/state->zoom - 1/old_zoom;
		state->x_offset = fmaf(fmaf((float)fb->w, -.5f, win->cur.x.pos),
			mul, state->x_offset);
		state->y_offset = fmaf(fmaf((float)fb->h, -.5f, win->cur.y.pos),
			mul, state->y_offset);
	} else {
		if (horz) {
			x = y;
			y = 0;
		}
		ev->image = move_state(state,
			(float)x * -WINDOW_FAST_MOVE_FACTOR,
			(float)y * -WINDOW_FAST_MOVE_FACTOR);
	}
}

void window_cursor_move(struct window_context *ctx, const double x,
const double y) {
	struct window_common *win = &ctx->win;
	if (win->cursor_pressed) {
		ctx->event.image = move_state(&ctx->image.state,
			(float)x - win->cur.x.pos, (float)y - win->cur.y.pos);
	}
	win->cur.x.pos = (float)x;
	win->cur.y.pos = (float)y;
}

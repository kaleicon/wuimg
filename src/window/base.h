// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2021 kaleido
#ifndef WU_WINDOW_BASE
#define WU_WINDOW_BASE

#include "misc/common.h"
#include "misc/term.h"
#include "opengl.h"
#include "dec.h"

#define WINDOW_KEYSTART ' '
#define WINDOW_KEYEND ('Z' + 1)

static const float WINDOW_FAST_MOVE_FACTOR = 2.0f;

enum key_action {
	key_released = 0,
	key_once = 1,
	key_pressed = 2,
};

struct window_keymap {
	bool active;
	bool shift;
	unsigned char map[WINDOW_KEYEND - WINDOW_KEYSTART];
};

struct wu_event {
	int cycle;
	enum rm_status {
		rm_never,
		rm_no,
		rm_ask,
		rm_yes,
	} rm:8;
	bool exit;
	enum image_event image:8;
};

struct window_cursor_axis {
	float pos;
};

struct window_cursor {
	struct window_cursor_axis x, y;
};

struct window_context;

typedef void (*window_fn_title_t)(struct window_context *ctx, const char *title);
typedef void (*window_fn_resize_t)(struct window_context *ctx, int w, int h);
typedef void (*window_fn_fullscreen_t)(struct window_context *ctx, int is_fullscreen);
typedef void (*window_fn_poll_t)(struct window_context *ctx, int msecs);
typedef void (*window_fn_ctx_t)(struct window_context *ctx);

struct window_fn {
	window_fn_title_t title;
	window_fn_resize_t resize;
	window_fn_fullscreen_t fullscreen;
	window_fn_poll_t poll;
	window_fn_ctx_t swap_buffers;
	window_fn_ctx_t terminate;
};

struct window_timers {
	struct timespec key, play;
};

struct window_common {
	void *ctx;
	struct window_fn fn;
	struct window_cursor cur;
	struct window_timers timer;
	uint32_t refresh_nsec;
	bool playing;
	bool cursor_pressed;
	bool focused;
	bool fullscreen;
	struct window_keymap held_keys;
	const char *backend_name;
};

struct window_context {
	struct gl_context gl;
	struct wudec_image image;
	struct term_queue term;
	struct wu_event event;
	struct window_common win;
};

struct window_offscreen;

typedef void (*window_fn_offctx_t)(struct window_offscreen *ctx);

struct window_offscreen {
        void *ctx;
        window_fn_offctx_t terminate;
        const char *backend_name;
};

uint32_t window_refresh_nsec(const struct window_context *ctx);

unsigned char * window_keymap_map(struct window_keymap *held_keys);

void window_key_lift(struct window_context *ctx);

void window_key_add(struct window_context *ctx, enum key_action action,
int code, bool shift);

enum trit window_size_update(struct window_context *ctx, int w, int h);

void window_cursor_scroll(struct window_context *ctx, double x, double y,
bool horz, bool zoom);

void window_cursor_move(struct window_context *ctx, double x, double y);

#endif /* WU_WINDOW_BASE */

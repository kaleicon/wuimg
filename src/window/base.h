// SPDX-License-Identifier: 0BSD
#ifndef WU_WINDOW_BASE
#define WU_WINDOW_BASE

#include "misc/common.h"

#include "wudefs.h"
#include "opengl.h"

#define WINDOW_KEYSTART ' '
#define WINDOW_KEYEND ('Z' + 1)

enum key_action {
	key_release = 0,
	key_external = 1,
	key_press = 2,
};

struct window_keymap {
	bool shift;
	unsigned char map[WINDOW_KEYEND - WINDOW_KEYSTART];
};

struct wu_event {
	int cycle;
	enum rm_status {
		rm_no = 0,
		rm_disable,
		rm_ask,
		rm_yes,
	} rm:8;
	enum wu_program_event {
		wu_program_none = 0,
		wu_program_exit,
		wu_program_reload_file,
	} program:8;
	enum image_event image:8;
};

struct window_cursor_axis {
	float pos;
	float scroll;
};

struct window_cursor {
	struct window_cursor_axis x, y;
};

typedef void (*window_fn_title_t)(void *ctx, const char *title);
typedef void (*window_fn_resize_t)(void *ctx, int w, int h);
typedef void (*window_fn_fullscreen_t)(void *ctx, int enable);
typedef void (*window_fn_ctx_t)(void *ctx);

struct window_fn {
	window_fn_title_t title;
	window_fn_resize_t resize;
	window_fn_fullscreen_t fullscreen;
	window_fn_ctx_t poll;
	window_fn_ctx_t swap_buffers;
	window_fn_ctx_t terminate;
};

struct window_common {
	struct window_fn fn;
	struct window_cursor cur;
	uint32_t refresh_nsec;
	bool wait_redraw;
	bool pressed;
	bool focused;
	bool fullscreen;
};

struct window_public {
	struct gl_context gl;
	struct window_common win;
	struct timespec timer;
	struct image_context image;
	struct wu_event event;
	struct window_keymap held_keys;
};

void window_key_lift(struct window_keymap *held_keys);

void window_key_add(struct window_keymap *held_keys, enum key_action action,
int code, bool shift);

enum trit window_size_update(struct window_public *pub, int w, int h);

void window_scroll_axis(struct window_cursor_axis *axis, double offset);

void window_scroll(struct window_cursor *cursor, double x, double y);

void window_cursor_move(struct window_public *pub, double x, double y);

#endif /* WU_WINDOW_BASE */

#ifndef WU_WINDOW_BASE
#define WU_WINDOW_BASE

#include "common/common.h"
#include "window/egl.h"

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
	enum trit rm:8;
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

struct window_common {
	struct egl egl;
	struct window_cursor cur;
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

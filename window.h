#ifndef WINDOW
#define WINDOW

#include "wudefs.h"
#include "opengl.h"
#include "events.h"

struct window_geometry {
	int x, y, w, h;
};

struct screen_properties {
	struct window_geometry geom;
	int refresh_rate;
	bool fullscreen;
	bool has_focus;
};

struct window_control {
	void *window;
	struct gl_context context;
	struct wu_pos file;
	struct wu_state state;
	struct wu_event event;
	struct wu_conf conf;
	struct screen_properties screen;
};

void terminate_window(void);

void poll_window(struct window_control *control);

void redraw_window(void *window);

void set_window_title(void *window, const char *filename);

void set_fullscreen_window(struct window_control *control);

bool setup_window(struct window_control *control);

#endif /* WINDOW */

#ifndef WINDOW
#define WINDOW

#include <GLFW/glfw3.h>

#include "wudefs.h"
#include "opengl.h"

struct window_geometry {
	int window_x, window_y;
	int window_w, window_h;
	int monitor_w, monitor_h;
	int refresh_rate;
	bool fullscreen;
	bool has_focus;
};

struct window_control {
	GLFWwindow *window;
	struct gl_context context;
	struct window_geometry display;
	struct wu_state state;
	struct wu_conf conf;
	struct wu_event event;
};

void end_display(const struct window_control *control);

bool window_loop(struct window_control *control, struct image_file *file,
const char *filename, const bool one_file_in_list);

bool setup_display(struct window_control *control);

#endif /* WINDOW */

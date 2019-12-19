#ifndef WINDOW
#define WINDOW

#include <epoxy/gl.h>
#include <GLFW/glfw3.h>

#include "opengl.h"

enum anim_state {
	none = 0,
	playing = 1,
	paused = 3, // Toggle with ^ 2
};

struct window_control {
	struct gl_context *context;
//	int max_w;
//	int max_h;
	enum anim_state anim;
	int cycle;
	int cycle_sub_img;
	bool cycle_wait;
};

int get_monitor_refresh_rate();

bool update_window(GLFWwindow *window, struct window_control *control,
const struct image_file *file, const int idx);

void setup_window(GLFWwindow *window, struct window_control *control);

GLFWwindow * create_window();

#endif /* WINDOW */

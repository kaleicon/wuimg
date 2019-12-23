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

struct window_geometry {
	int xpos, ypos;
	int width, height;
};

struct window_control {
	struct gl_context *context;
	struct window_geometry windowed_state;
//	int max_w;
//	int max_h;
	int cycle;
	int cycle_sub_img;
	int refresh_rate;
	enum anim_state anim;
	bool cycle_wait;
	bool fullscreen;
	bool rm;
	bool reload;
};

bool update_window(GLFWwindow *window, struct window_control *control,
const struct image_file *file, const int idx, const bool first_load);

void setup_window(GLFWwindow *window, struct window_control *control);

GLFWwindow * create_window();

#endif /* WINDOW */

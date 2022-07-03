#ifndef WU_WINDOW_GLFW
#define WU_WINDOW_GLFW

#include "base.h"

#include <GLFW/glfw3.h>

struct glfw_context {
	struct window_public *pub;
	GLFWwindow *window;
	struct window_geom {
		int x, y, w, h;
	} geom;
};

void glfw_fullscreen(struct glfw_context *glfw, bool is_fullscreen);

const char * glfw_setup(struct glfw_context *glfw, struct window_public *pub);

#endif /* WU_WINDOW_GLFW */

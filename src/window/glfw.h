// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2021 kaleido
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

const char * glfw_setup(struct glfw_context *glfw, struct window_public *pub);

#endif /* WU_WINDOW_GLFW */

// SPDX-License-Identifier: 0BSD
#ifndef WU_WRITE
#define WU_WRITE

#include "wudefs.h"
#include "window.h"
#include "opengl.h"

struct write_args {
	const char *outdir;
	bool overwrite;
	bool stdout;
	bool null;
};

struct write_writer {
	bool gl_initialized;
	struct wuimg out;
	struct window_offscreen window;
	struct gl_context gl;
};

void write_writer_terminate(struct write_writer *writer);

bool write_image(struct image_context *image, struct write_writer *writer,
const struct write_args *args);

int write_args(int argc, char **argv, struct write_args *args);

#endif // WU_WRITE

// SPDX-License-Identifier: 0BSD
#ifndef WRITE_PAM
#define WRITE_PAM

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
	struct window_offscreen window;
	struct gl_context gl;
	struct gl_reader reader;
	struct write_out {
		size_t base_len;
		int dirfd;
		bool with_idx;
		char name[256];
	} out;
};

void write_writer_terminate(struct write_writer *writer);

enum wu_error write_image(struct image_context *image,
struct write_writer *writer, const struct write_args *args);

bool write_writer_init(struct write_writer *writer, struct wu_conf *wuconf);

int write_args(int argc, char **argv, struct write_args *args);

#endif /* WRITE_PAM */

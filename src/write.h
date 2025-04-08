// SPDX-License-Identifier: 0BSD
#ifndef WU_WRITE
#define WU_WRITE

#include "wudefs.h"

extern const char write_description[];
extern const char write_switches[];

struct write_args {
	const char *outdir;
	bool overwrite;
	bool stdout;
	bool null;
};

typedef void (*writer_close_t)(void *state);
typedef uint8_t * (*writer_get_row_t)(void *state, size_t y);
typedef const char * (*writer_set_image_t)(void *state,
	const struct wuimg *dst, const struct wuimg *src, bool read_only);

struct write_writer {
	void *state;
	writer_close_t close;
	writer_get_row_t get_row;
	writer_set_image_t set_image;
};

bool write_image(struct image_context *image, const struct write_args *args,
struct write_writer *writer);

int write_filelist(const struct write_args *args, struct write_writer *writer,
int len, char **names, const struct wu_conf *conf);

int write_args(int argc, char **argv, struct write_args *args);

#endif // WU_WRITE

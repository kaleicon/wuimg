// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2020 kaleido
#ifndef WU_WRITE
#define WU_WRITE

#include "dec.h"

extern const char write_description[];
extern const char write_switches[];

struct write_args {
	const char *outdir;
	bool overwrite;
	bool stdout;
	bool null;
	int codec;
};

typedef void (*writer_close_t)(void *state);
typedef void (*writer_get_row_t)(void *state, size_t y, void *restrict tgt);
typedef const char * (*writer_set_image_t)(void *state,
	const struct wuimg *dst, const struct wuimg *src);

struct write_writer {
	void *state;
	writer_close_t close;
	writer_get_row_t get_row;
	writer_set_image_t set_image;
};

int write_filelist(const struct write_args *args, struct write_writer *writer,
int len, char **names, const struct wu_conf *conf);

int write_args(int argc, char **argv, struct write_args *args);

#endif // WU_WRITE

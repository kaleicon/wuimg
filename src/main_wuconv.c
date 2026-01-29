// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2024 kaleido
#include <stdio.h>
#include <string.h>

#include "misc/common.h"
#include "fmtmap.h"
#include "imgconv.h"
#include "opts.h"
#include "write.h"

#define HELP_SHORT "-h"
#define HELP_LONG "--help"
#define FMTS_LONG "--fmts"

#define WUCONV_CANON_NAME "wuconv"

static void close_conv(void *state) {
	imgconv_close(state);
}
static void get_row(void *state, size_t y, void *restrict tgt) {
	imgconv_get_row(state, y, tgt);
}
static const char * init_conv(void *state, const struct wuimg *dst,
const struct wuimg *src) {
	return imgconv_init(state, dst, src);
}

enum info_opts_c {
	wo_done = 0,
	wo_h = 'h',
	wo_fmts = 0x80,
};

static const struct opts info_opts[] = {
	{wo_h, "help", "",
		"\t\tYou are here."},
	{wo_fmts, "fmts", "",
		"\t\tPrint supported formats."},
};

static void print_help(FILE *ofp) {
	fputs(
		"Usage:\n"
		"\t" WUCONV_CANON_NAME " [switches] FILE|- [FILE ...]\n\n", ofp);
	fprintf(ofp, "Description:\n%s\n", write_description);

	opts_help("Program info:", info_opts, ARRAY_LEN(info_opts), ofp);
	write_help(
		"\n"
		"Conversion switches:", ofp);
}

int main(const int argc, char **argv) {
	struct write_args wargs = {0};
	int read = 1;
	const char *err = write_args(argc, argv, &read, &wargs);
	if (err) {
		fputs(err, stderr);
		return 1;
	}

	const enum info_opts_c c = opts_next(argc, argv, &read, info_opts,
		ARRAY_LEN(info_opts));
	switch (c) {
	case wo_h:
		print_help(stderr);
		return 0;
	case wo_fmts:
		fmtmap_print_known(stderr);
		return 0;
	case wo_done: break;
	}

	read += read < argc && !strcmp("--", argv[read]);
	struct imgconv conv;
	struct write_writer writer = {
		.state = &conv,
		.set_image = init_conv,
		.get_row = get_row,
		.close = close_conv,
	};
	return write_filelist(&wargs, &writer, argc - read, argv + read, NULL);
}

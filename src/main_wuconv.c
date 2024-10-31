// SPDX-License-Identifier: 0BSD
#include <stdio.h>

#include "fmtmap.h"
#include "imgconv.h"
#include "write.h"

#define HELP_SHORT "-h"
#define HELP_LONG "--help"
#define FMTS_LONG "--fmts"

#define WUCONV_CANON_NAME "wuconv"

enum what_to_do {
	convert_images,
	show_help,
	show_fmts,
};

static int print_help(FILE *ofp) {
	fputs("Usage:\n"
		"\t" WUCONV_CANON_NAME " [switches] FILE|- [FILE ...]\n\n", ofp);
	fprintf(ofp, "Description:\n%s\n", write_description);
	fputs(
		"Program info:\n"
		"\t" HELP_SHORT " | " HELP_LONG "\n"
		"\t\tYou are here.\n"

		"\t" FMTS_LONG "\n"
		"\t\tPrint supported formats.\n"

		"\n"
		"conversion switches:\n",
		ofp);
	return fputs(write_switches, ofp);
}

static enum what_to_do first_arg(int argc, char **argv) {
	if (argc <= 0
	|| !strcmp(argv[0], HELP_SHORT) || !strcmp(argv[0], HELP_LONG)) {
		return show_help;
	} else if (!strcmp(argv[0], FMTS_LONG)) {
		return show_fmts;
	}
	return convert_images;
}

static void close_conv(void *ptr) {
	imgconv_close(ptr);
}
static uint8_t * get_row(void *ptr, size_t y) {
	return imgconv_get_row(ptr, y);
}
static const char * init_conv(void *ptr, const struct wuimg *dst,
const struct wuimg *src) {
	return imgconv_init(ptr, dst, src);
}

int main(const int argc, char **argv) {
	struct write_args wargs = {0};
	int read = 1;
	switch(first_arg(argc - read, argv + read)) {
	case convert_images:
		;const int r = write_args(argc - read, argv + read, &wargs);
		if (r >= 0) {
			read += r;
			break;
		}
		// fallthrough
	case show_help:
		print_help(stderr);
		return 0;
	case show_fmts:
		fmtmap_print_known(stderr);
		return 0;
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

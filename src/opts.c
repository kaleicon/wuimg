// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2026 kaleido
#include <string.h>

#include "opts.h"

#define MAYBE_U_FORGOT " (maybe it wasn't compiled in?)"
const char OPTS_UNKNOWN_DECODER[] = "unknown decoder" MAYBE_U_FORGOT,
        OPTS_UNKNOWN_ENCODER[] = "unknown encoder" MAYBE_U_FORGOT;

static void print_argname(const struct opts *opt, FILE *out) {
	if (opt->argname[0]) {
		fprintf(out, " %.*s", (int)sizeof(opt->argname), opt->argname);
	}
}

void opts_help(const char *preamble, const struct opts *opts,
const size_t opt_len, FILE *out) {
	fputs(preamble, out);
	fputc('\n', out);
	for (size_t i = 0; i < opt_len; ++i) {
		const struct opts *opt = opts + i;
		fputc('\t', out);
		if (opt->c > ' ' && opt->c < 0x80) {
			fprintf(out, "-%c", opt->c);
			print_argname(opt, out);
			fputs(opt->name[0] ? ", " : "\n", out);
		}
		if (opt->name[0]) {
			fprintf(out, "--%.*s", (int)sizeof(opt->name), opt->name);
			print_argname(opt, out);
			fputc('\n', out);
		}
		fputs(opt->help, out);
		fputc('\n', out);
	}
}

static uint8_t opts_check(const int argc, int *idx,
const struct opts *opt) {
	if (opt->argname[0]) {
		if (argc - *idx <= 1) {
			return 0;
		}
		*idx += 1;
	}
	return opt->c;
}

uint8_t opts_next(const int argc, char *const *argv, int *idx,
const struct opts *opts, const size_t opt_len) {
	if (*idx >= argc) {
		return 0;
	}
	const char *arg = argv[*idx];
	if (arg[0] != '-' || arg[1] == 0) {
		return 0;
	}
	const uint8_t c = (uint8_t)arg[1];
	if (!arg[2]) { // short option
		for (size_t i = 0; i < opt_len; ++i) {
			if (opts[i].c == c) {
				return opts_check(argc, idx, opts + i);
			}
		}
	} else {
		if (c != '-') {
			return 0;
		}
		for (size_t i = 0; i < opt_len; ++i) {
			if (!strncmp(arg + 2, opts[i].name, sizeof(opts[i].name))) {
				return opts_check(argc, idx, opts + i);
			}
		}
	}
	return 0;
}

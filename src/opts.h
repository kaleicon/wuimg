// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2026 kaleido
#ifndef WU_OPTS
#define WU_OPTS

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

struct opts {
	uint8_t c;
	char name[7];
	char argname[8];
	const char *help;
};

enum opts_global {
	opts_global_done = 0,
	opts_global_help = 'h',
	opts_global_type = 't',
	opts_global_fmts = 0x80,
	opts_global_envs,
};

static const struct opts OPTS_HELP = {
	opts_global_help, "help", "",
		"\t\tYou are here.",
}, OPTS_FMTS = {
	opts_global_fmts, "fmts", "",
		"\t\tPrint supported formats.",
}, OPTS_ENVS = {
	opts_global_envs, "envs", "",
		"\t\tPrint recognized environment variables.",
}, OPTS_TYPE = {
	opts_global_type, "type", "ID",
		"\t\tForce input decoder. ID must be one of the decoders\n"
		"\t\tlisted with `--fmts`.",
};

extern const char OPTS_UNKNOWN_DECODER[], OPTS_UNKNOWN_ENCODER[];

void opts_help(const char *preamble, const struct opts *opts, size_t opt_len,
FILE *out);

uint8_t opts_next(int argc, char *const *argv, int *idx,
const struct opts *opts, size_t opt_len);

#endif /* WU_OPTS */

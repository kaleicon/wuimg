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

void opts_help(const char *preamble, const struct opts *opts, size_t opt_len,
FILE *out);

uint8_t opts_next(int argc, char *const *argv, int *idx,
const struct opts *opts, size_t opt_len);

#endif /* WU_OPTS */

// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2021 kaleido
#ifndef WU_EXTRACT
#define WU_EXTRACT

#include <stdio.h>
#include <stdbool.h>

#include <archive.h>

struct extract_iter {
	struct archive *ra;
	FILE *cur;
	char *name;
	long idx;
	long total;
	int fd;
	bool seen_it_all;
};

void extract_free(struct extract_iter *iter);

bool extract_file(struct extract_iter *iter, long idx, bool wraparound);

const char * extract_init(struct extract_iter *iter, const char *filename);

#endif /* WU_EXTRACT */

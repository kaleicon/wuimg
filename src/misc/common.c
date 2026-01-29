// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2019 kaleido
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include <unistd.h>

#include "misc/common.h"

long num_cpus(void) {
#if defined(_SC_NPROCESSORS_ONLN)
	return sysconf(_SC_NPROCESSORS_ONLN); // Linux
#elif defined(SC_NPROCESSORS_ONLN)
	return sysconf(SC_NPROCESSORS_ONLN); // FreeBSD
#else
	return 1; // sorry
#endif
}

static bool is_smol(const size_t nmemb, const size_t size) {
	const size_t limit = 0x7fffff;
	return nmemb && limit/nmemb > size;
}

void * small_realloc(void *ptr, const size_t nmemb, const size_t size) {
	return is_smol(nmemb, size) ? realloc(ptr, nmemb * size) : NULL;
}

void * small_calloc(const size_t nmemb, const size_t size) {
	return is_smol(nmemb, size) ? calloc(nmemb, size) : NULL;
}

void * small_malloc(const size_t nmemb, const size_t size) {
	return is_smol(nmemb, size) ? malloc(nmemb * size) : NULL;
}

void fatal_bug(const char *name, const char *msg) {
	fprintf(stderr, "Fatal bug!\n%s: %s\n", name, msg);
	abort();
}

void null_function() {}

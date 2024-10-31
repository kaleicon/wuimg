// SPDX-License-Identifier: 0BSD
#ifndef COMMON_FUNCS
#define COMMON_FUNCS

#include <stddef.h>
#include <stdint.h>

#define WU_CANON_NAME "wu"
#define ARRAY_LEN(arr) ( sizeof(arr) / sizeof(*arr) )

enum trit {
	trit_false = 0,
	trit_true = 1,
	trit_what = 2,
};

uint8_t short_opt(const char *opt);

long num_cpus(void);

void * small_realloc(void *ptr, size_t nmemb, size_t size);

void * small_calloc(size_t nmemb, size_t size);

void * small_malloc(size_t nmemb, size_t size);

void fatal_bug(const char *name, const char *msg);

void null_function();

#endif /* COMMON_FUNCS */

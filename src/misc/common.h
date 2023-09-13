// SPDX-License-Identifier: 0BSD
#ifndef COMMON_FUNCS
#define COMMON_FUNCS

#include <inttypes.h>
#include <stdbool.h>
//#include <stdint.h>
#include <stdio.h>
#include <time.h>

#define WU_CANON_NAME "wu"
#define ARRAY_LEN(arr) ( sizeof(arr) / sizeof(*arr) )

enum trit {
	trit_false = 0,
	trit_true = 1,
	trit_what = 2,
};

typedef uint64_t watch_t;

void rfc3339_format(time_t t, FILE *out);

time_t utc_to_epoch(int year, int month, int day, int hour, int minute,
int second);

void nanosec_report(const char *ocurrence, watch_t elapsed);

watch_t watch_look(void);

watch_t watch_elapsed(watch_t start);

watch_t watch_report(const char *ocurrence, watch_t start);

long num_cpus(void);

void fatal_bug(const char *name, const char *msg);

#endif /* COMMON_FUNCS */

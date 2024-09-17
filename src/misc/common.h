// SPDX-License-Identifier: 0BSD
#ifndef COMMON_FUNCS
#define COMMON_FUNCS

#include <stdint.h>
#include <stdio.h>
#include <time.h>

#define WU_CANON_NAME "wu"
#define ARRAY_LEN(arr) ( sizeof(arr) / sizeof(*arr) )

struct display_dims {
	int w, h;
};

enum trit {
	trit_false = 0,
	trit_true = 1,
	trit_what = 2,
};

enum report_level {
	report_always = -1,
	report_normal = 0,
	report_info = 1,
	report_detail = 2,
	report_whocares = 3,
};

typedef uint64_t watch_t;

void rfc3339_format(time_t t, FILE *out);

time_t utc_to_epoch(int year, int month, int day, int hour, int minute,
int second);

void nanosec_report(const char *ocurrence, watch_t elapsed,
enum report_level level);

watch_t watch_look(void);

watch_t watch_elapsed(watch_t start);

watch_t watch_report(const char *ocurrence, watch_t start,
enum report_level level);

uint8_t short_opt(const char *opt);

long num_cpus(void);

void fatal_bug(const char *name, const char *msg);

void null_function();

#endif /* COMMON_FUNCS */

#ifndef COMMON_FUNCS
#define COMMON_FUNCS

#include <stdio.h>
#include <stdbool.h>
#include <time.h>

#define WU_CANON_NAME "wu"
#define ARRAY_LEN(arr) ( sizeof(arr) / sizeof(*arr) )

enum trit {
	trit_false = 0,
	trit_true = 1,
	trit_what = 2,
};

void rfc3339_format(time_t t, FILE *out);

time_t utc_to_epoch(int year, int month, int day, int hour, int minute,
int second);

double clock_ellapsed(clock_t start);

clock_t clock_print(const char *ocurrence, clock_t start);

void fatal_bug(const char *name, const char *msg);

#endif /* COMMON_FUNCS */

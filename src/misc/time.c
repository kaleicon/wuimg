// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2019 kaleido
#include <inttypes.h>
#include <stdbool.h>
#include <stdlib.h>

#include "misc/time.h"

// A straw broke my camel's back so I wrote my own time functions.
static const long DAYS_BETWEEN_1970_2000 = 365*30 + 30/4; // 10957 btw

void rfc3339_format(time_t t, FILE *out) {
	// Format UNIX time as "y-m-d h:m:sZ"

	// Set our epoch to the first of March, 2000.
	t -= (DAYS_BETWEEN_1970_2000 + 31 + 29) * 86400;

	long days = (long)(t / 86400);
	long secs = (long)(t % 86400);
	if (secs < 0) {
		secs += 86400;
		--days;
	}

	const long days_in_cycle = 365*400 + 97;
	const long days_in_century = 365*100 + 24;
	const long days_in_four_years = 365*4 + 1;
	const long days_in_a_year = 365;

	long greg_cycles = days / days_in_cycle;
	days = days % days_in_cycle;
	if (days < 0) {
		days += days_in_cycle;
		--greg_cycles;
	}

	long centuries = days / days_in_century;
	if (centuries == 4) {
		--centuries;
	}
	days -= centuries * days_in_century;

	long leaps = days / days_in_four_years;
	if (leaps == 25) {
		--leaps;
	}
	days -= leaps * days_in_four_years;

	long years = days / days_in_a_year;
	if (years == 4) {
		--years;
	}
	days -= years * days_in_a_year;
	years += 4*leaps + 100*centuries + 400*greg_cycles;

	const unsigned char month_days[] = {
	//	mar,apr,may,jun,jul,aug,sep,oct,nov,dec,jan,feb
		31, 30, 31, 30, 31, 31, 30, 31, 30, 31, 31, 29
	};
	long months = 0;
	while (month_days[months] <= days) {
		days -= month_days[months];
		++months;
	}

	// Back to the real world
	years += 2000;
	months += 3;
	if (months > 12) {
		months -= 12;
		++years;
	}
	days += 1;

	const long hours = secs / 3600;
	const long minutes = secs / 60 % 60;
	secs %= 60;

	fprintf(out, "%ld-%.2ld-%.2ld %.2ld:%.2ld:%.2ldZ",
		years, months, days, hours, minutes, secs);
}

// Plug sane numbers into a function and get a time_t back. Wow. So hard.
time_t utc_to_epoch(int year, int month, int day, int hour, int minute,
int second) {
	month -= 1;
	if (month >= 12 || month < 0) {
		year += month / 12;
		month = month % 12;
		if (month < 0) {
			month += 12;
			--year;
		}
	}

	const int millenial_year = year - 2000;
	int greg_cycles = millenial_year / 400;
	int rem = millenial_year % 400;
	if (rem < 0) {
		--greg_cycles;
		rem += 400;
	}

	int centuries = rem / 100;
	rem -= centuries * 100;

	int leap_days = rem / 4;
	bool is_leap = (rem % 4 == 0);
	leap_days += 97 * greg_cycles + 24 * centuries - is_leap;

	time_t days_since_epoch = millenial_year * 365 + leap_days
		+ DAYS_BETWEEN_1970_2000;

	const unsigned char days_in_this_year[] = {
		31, 28 + is_leap, 31, 30, 31, 30,
		31, 31,           30, 31, 30, 31
	};
	for (int i = 0; i < month; ++i) {
		days_since_epoch += days_in_this_year[i];
	}

	days_since_epoch += day;
	return (days_since_epoch * 24 * 60 * 60)
		+ (hour * 60 * 60)
		+ (minute * 60)
		+ second;
}

time_t time_from_dos(time_t dos_ts) {
	return dos_ts + 315532800; // 1980-01-01Z
}

void nanosec_report(const char *ocurrence, const watch_t elapsed,
const enum report_level level) {
	int verbose = 0;
	const char *v = getenv("WU_TIMING");
	if (v) {
		verbose = atoi(v);
	}
	if (verbose >= level) {
		fprintf(stderr, "%s in %" PRIu64 " ns\n", ocurrence, elapsed);
	}
}

watch_t watch_look(void) {
	const clockid_t cl =
#if _POSIX_CPUTIME > 0
	CLOCK_PROCESS_CPUTIME_ID
#elif _POSIX_MONOTONIC_CLOCK > 0
	CLOCK_MONOTONIC
#else
	CLOCK_REALTIME
#endif
	;
	struct timespec ts;
	clock_gettime(cl, &ts);
	return (watch_t)(ts.tv_sec * 1000000000 + ts.tv_nsec);
}

watch_t watch_elapsed(const watch_t start) {
	return watch_look() - start;
}

watch_t watch_report(const char *ocurrence, const watch_t start,
const enum report_level level) {
	const watch_t end = watch_look();
	nanosec_report(ocurrence, end - start, level);
	return end;
}

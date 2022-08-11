#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>

#include <unistd.h>
#include <sys/mman.h>

#include "common.h"

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

	long years = days / 365;
	if (years == 4) {
		--years;
	}
	days -= years * 365;
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

double clock_ellapsed(const clock_t start) {
	return (double)(clock() - start) / CLOCKS_PER_SEC;
}

clock_t clock_print(const char *ocurrence, const clock_t start) {
	const clock_t end = clock();
	fprintf(stderr, "%s in %f seconds\n", ocurrence,
		(double)(end - start) / CLOCKS_PER_SEC);
	return end;
}

long lmod(const long val, const long max) {
	return (val % max + max) % max;
}

int imod(const int val, const int max) {
	return (val % max + max) % max;
}

static size_t zulog2(size_t x) {
	size_t acc = 0;
	while ((x >>= 1)) {
		++acc;
	}
	return acc;
}

unsigned int ulog2(unsigned int x) {
	unsigned int acc = 0;
	while ((x >>= 1)) {
		++acc;
	}
	return acc;
}

size_t zumax(const size_t x, const size_t y) {
	return x > y ? x : y;
}

size_t zumin(const size_t x, const size_t y) {
	return x < y ? x : y;
}

unsigned int umax(const unsigned int x, const unsigned int y) {
	return x > y ? x : y;
}

unsigned int umin(const unsigned int x, const unsigned int y) {
	return x < y ? x : y;
}

long lmax(const long x, const long y) {
	return x > y ? x : y;
}

long lmin(const long x, const long y) {
	return x < y ? x : y;
}

int imax(const int x, const int y) {
	return x > y ? x : y;
}

int imin(const int x, const int y) {
	return x < y ? x : y;
}

int iclamp(const int n, const int min, const int max) {
	if (n < min) {
		return min;
	} else if (n > max) {
		return max;
	}
	return n;
}

align_t align_from_int(const size_t alignment) {
	return (align_t)zulog2(alignment);
}

size_t scanline_length(const size_t width, const uint8_t bitdepth,
const align_t align_sh) {
	const size_t bytes = (width * bitdepth + 7) / 8;
	const size_t a = ~0lu << align_sh;
	return (bytes + ~a) & a;
}

align_t scanline_alignment(const size_t stride, const size_t width,
const uint8_t bitdepth) {
	const size_t base = scanline_length(width, bitdepth, 0);
	if (stride >= base) {
		const size_t diff = stride - base;
		if (diff) {
			return (align_t)(zulog2(diff) + 1);
		}
		return 0;
	}
	return -1;
}

void * memdup(const void *s, size_t n) {
	void *d = malloc(n);
	if (d) {
		memcpy(d, s, n);
	}
	return d;
}

#ifndef _GNU_SOURCE
void * memrchr(const void *s, const int c, size_t n) {
	const unsigned char *data = s;
	while (n) {
		--n;
		if (data[n] == c) {
			return (void *)(data + n);
		}
	}
	return NULL;
}
#endif

int unmap_file(struct map_info *mm) {
	if (mm->data) {
		return munmap((void *)mm->data, mm->len);
	}
	return 0;
}

static bool map_common(struct map_info *mm, const int fd, const off_t end) {
	const size_t len = (size_t)end;
	void *data = mmap(NULL, len, PROT_READ, MAP_PRIVATE, fd, 0);
	if (data == MAP_FAILED) {
		*mm = (struct map_info) {
			.len = 0,
			.data = NULL,
		};
		return false;
	}
	*mm = (struct map_info) {
		.len = len,
		.data = data,
	};
	return true;
}

bool map_file(struct map_info *mm, FILE *ifp) {
	fseek(ifp, 0, SEEK_END);
	return map_common(mm, fileno(ifp), ftello(ifp));
}

bool map_file_fd(struct map_info *mm, const int fd) {
	return map_common(mm, fd, lseek(fd, 0, SEEK_END));
}

void fatal_bug(const char *name, const char *msg) {
	fprintf(stderr, "Fatal bug!\n%s: %s\n", name, msg);
	abort();
}

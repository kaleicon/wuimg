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

// Plug sane numbers into a struct and get a time_t back. Wow. So hard.
time_t utc_to_epoch(const struct utc_time *tm) {
	int year = tm->year;
	int mon = tm->mon - 1;
	if (mon >= 12 || mon < 0) {
		year += mon / 12;
		mon = mon % 12;
		if (mon < 0) {
			mon += 12;
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
	for (int i = 0; i < mon; ++i) {
		days_since_epoch += days_in_this_year[i];
	}

	days_since_epoch += tm->day;
	return (days_since_epoch * 24 * 60 * 60)
		+ (tm->hour * 60 * 60)
		+ (tm->min * 60)
		+ tm->sec;
}

double clock_ellapsed(const clock_t start) {
	return (double)(clock() - start) / CLOCKS_PER_SEC;
}

size_t scanline_length(const size_t width, const size_t bitdepth,
size_t alignment) {
	const size_t bytes = (width * bitdepth + 7) / 8;
	--alignment;
	return (bytes + alignment) & (~alignment);
}

long lmod(const long val, const long max) {
	return (val % max + max) % max;
}

int imod(const int val, const int max) {
	return (val % max + max) % max;
}

size_t zulog2(size_t x) {
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

float fclampf(const float n, const float min, const float max) {
	if (n < min) {
		return min;
	} else if (n > max) {
		return max;
	}
	return n;
}

enum endianness which_end(void) {
	/* This is not UB after C99, except for traps representations, so it
	 * may be troublesome still, but there don't seem to be alternatives. */
	union {
		unsigned int ui;
		unsigned char uc[sizeof(unsigned int)];
	} test = {.ui = 1};
	return (enum endianness)test.uc[0];
}

uint32_t endian32(const uint32_t val, const enum endianness e) {
	const enum endianness native = which_end();
	if (native != e) {
		return (uint32_t)(val << 24
			| (val & 0x00ff00) << 8
			| (val & 0xff0000) >> 8
			| val >> 24);
	}
	return val;
}

uint16_t endian16(const uint16_t val, const enum endianness e) {
	const enum endianness native = which_end();
	if (native != e) {
		return (uint16_t)(val << 8 | val >> 8);
	}
	return val;
}

uint32_t buf_endian32(const void *restrict data, const enum endianness e) {
	const uint8_t *restrict d = data;
	switch (e) {
	case big_endian:
		return (uint32_t)(d[0]<<24 | d[1]<<16 | d[2]<<8 | d[3]);
	default:
		return (uint32_t)(d[3]<<24 | d[2]<<16 | d[1]<<8 | d[0]);
	}
}

uint16_t buf_endian16(const void *restrict data, const enum endianness e) {
	const uint8_t *restrict d = data;
	switch (e) {
	case big_endian:
		return (uint16_t)(d[0] << 8 | d[1]);
	default:
		return (uint16_t)(d[1] << 8 | d[0]);
	}
}

void loop_endian32(uint32_t *data, const enum endianness e, const size_t cnt) {
	for (size_t i = 0; i < cnt; ++i) {
		data[i] = endian32(data[i], e);
	}
}

void loop_endian16(uint16_t *data, const enum endianness e, const size_t cnt) {
	for (size_t i = 0; i < cnt; ++i) {
		data[i] = endian16(data[i], e);
	}
}

void * memdup(const void *s, size_t n) {
	void *d = malloc(n);
	if (d) {
		memcpy(d, s, n);
	}
	return d;
}

const void * memchk(const void *s, const unsigned char c, const size_t n) {
	const unsigned char *b = s;
	for (size_t m = 0; m < n; ++m) {
		if (b[m] != c) {
			return b + m;
		}
	}
	return NULL;
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

static size_t fread_alloc_common(struct memory *mem, const size_t len,
FILE *ifp) {
	mem->len = len;
	mem->data = malloc(len);
	if (mem->data) {
		return fread(mem->data, 1, len, ifp);
	}
	return 0;
}

size_t fread_alloc(struct memory *mem, const size_t len, FILE *ifp) {
	const size_t read = fread_alloc_common(mem, len, ifp);
	if (!read) {
		free(mem->data);
		mem->data = NULL;
	}
	return read;
}

size_t fread_alloc_strict(struct memory *mem, const size_t len, FILE *ifp) {
	const size_t read = fread_alloc_common(mem, len, ifp);
	if (read != len) {
		free(mem->data);
		mem->data = NULL;
		return 0;
	}
	return read;
}

long file_get_remaining(FILE *ifp) {
	const long cur = ftell(ifp);
	fseek(ifp, 0, SEEK_END);
	const long end = ftell(ifp);
	fseek(ifp, cur, SEEK_SET);
	return end - cur;
}

int munmap_file(struct mmap_info mm) {
	return munmap((void *)mm.data, mm.len);
}

static bool mmap_common(struct mmap_info *mm, const int fd, const off_t end) {
	const size_t len = (size_t)end;
	struct mmap_info m = {
		.len = len,
		.data = mmap(NULL, len, PROT_READ, MAP_SHARED, fd, 0),
	};
	memcpy(mm, &m, sizeof(m));
	return mm->data != MAP_FAILED;
}

bool mmap_file(struct mmap_info *mm, FILE *ifp) {
	fseek(ifp, 0, SEEK_END);
	return mmap_common(mm, fileno(ifp), ftello(ifp));
}

bool mmap_file_fd(struct mmap_info *mm, const int fd) {
	return mmap_common(mm, fd, lseek(fd, 0, SEEK_END));
}

char * id_template(const char *prefix, const size_t num) {
	const size_t len = strlen(prefix);
	size_t numlen = 1;
	for (size_t bound = 10; bound < num; bound *= 10) {
		++numlen;
	}

	char *id = malloc(len + numlen + 1);
	if (id) {
		sprintf(id, "%s%zu", prefix, num);
	}
	return id;
}

void fatal_bug(const char *name, const char *msg) {
	printf("%s: %s\n", name, msg);
	fflush(stdout);
	abort();
}

void null_function() {}

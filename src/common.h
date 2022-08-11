#ifndef COMMON_FUNCS
#define COMMON_FUNCS

#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
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

struct map_info {
	size_t len;
	const unsigned char *data;
};

typedef int8_t align_t;

void rfc3339_format(time_t t, FILE *out);

time_t utc_to_epoch(int year, int month, int day, int hour, int minute,
int second);

double clock_ellapsed(clock_t start);

clock_t clock_print(const char *ocurrence, clock_t start);

long lmod(long val, long max);

int imod(int val, int max);

unsigned int ulog2(unsigned int x);

size_t zumax(size_t x, size_t y);

size_t zumin(size_t x, size_t y);

unsigned int umax(unsigned int x, unsigned int y);

unsigned int umin(unsigned int x, unsigned int y);

long lmax(long x, long y);

long lmin(long x, long y);

int imax(int x, int y);

int imin(int x, int y);

int iclamp(int n, int min, int max);

align_t align_from_int(const size_t alignment);

size_t scanline_length(size_t width, uint8_t bitdepth, align_t alignment);

align_t scanline_alignment(size_t stride, size_t width, uint8_t bitdepth);

void * memdup(const void *s, size_t n);

#ifndef _GNU_SOURCE
void * memrchr(const void *s, int c, size_t n);
#endif

int unmap_file(struct map_info *mm);

bool map_file(struct map_info *mm, FILE *ifp);

bool map_file_fd(struct map_info *mm, int fd);

void fatal_bug(const char *name, const char *msg);

#endif /* COMMON_FUNCS */

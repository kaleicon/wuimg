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

union int_real {
	uint32_t bytes;
	float real;
};

enum endianness {
	big_endian = 0,
	little_endian = 1,
};

struct map_info {
	size_t len;
	const unsigned char *data;
};

void rfc3339_format(time_t t, FILE *out);

time_t utc_to_epoch(int year, int month, int day, int hour, int minute,
int second);

double clock_ellapsed(clock_t start);

void clock_print(const char *ocurrence, clock_t start);

size_t scanline_length(size_t width, uint8_t bitdepth, uint8_t alignment);

uint8_t scanline_alignment(size_t stride, size_t width, uint8_t bitdepth);

size_t subsamp(size_t dim, uint8_t sub);

long lmod(long val, long max);

int imod(int val, int max);

size_t zulog2(size_t x);

unsigned int ulog2(unsigned int x);

int ilog2(int x);

size_t zumax(size_t x, size_t y);

size_t zumin(size_t x, size_t y);

unsigned int umax(unsigned int x, unsigned int y);

unsigned int umin(unsigned int x, unsigned int y);

long lmax(long x, long y);

long lmin(long x, long y);

int imax(int x, int y);

int imin(int x, int y);

int iclamp(int n, int min, int max);

float fclampf(float n, float min, float max);

enum endianness which_end(void);

uint16_t endian16(uint16_t val, enum endianness e);

uint32_t endian32(uint32_t val, enum endianness e);

float endianf32(uint32_t val, enum endianness e);

uint16_t buf_endian16(const void *data, enum endianness e);

uint32_t buf_endian32(const void *data, enum endianness e);

float buf_endianf32(const void *data, enum endianness e);

void loop_endian16(uint16_t *data, enum endianness e, size_t cnt);

void loop_endian32(uint32_t *data, enum endianness e, size_t cnt);

void loop_endian64(uint64_t *data, enum endianness e, size_t cnt);

void * memdup(const void *s, size_t n);

#ifndef _GNU_SOURCE
void * memrchr(const void *s, int c, size_t n);
#endif

int unmap_file(struct map_info *mm);

bool map_file(struct map_info *mm, FILE *ifp);

bool map_file_fd(struct map_info *mm, int fd);

void fatal_bug(const char *name, const char *msg);

#endif /* COMMON_FUNCS */

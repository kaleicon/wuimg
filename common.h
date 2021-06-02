#ifndef COMMON_FUNCS
#define COMMON_FUNCS

#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include <sys/mman.h> /* MAP_FAILED macro */

#define ARRAY_LEN(arr) ( sizeof(arr) / sizeof(*arr) )

struct display_dims {
	unsigned w, h;
};

struct utc_time {
	int year, mon, day, hour, min, sec;
};

enum endianness {
	big_endian = 0,
	little_endian = 1,
};

struct mmap_file {
	size_t len;
	unsigned char *data;
};

void rfc3339_format(time_t t, FILE *out);

time_t utc_to_epoch(const struct utc_time *tm);

double clock_ellapsed(const clock_t start);

size_t scanline_length(size_t width, size_t bitdepth, size_t alignment);

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

float fclampf(float n, float min, float max);

enum endianness which_end(void);

uint32_t endian32(uint32_t val, enum endianness e);

uint16_t endian16(uint16_t val, enum endianness e);

uint16_t buf_endian16(const void *data, enum endianness e);

uint32_t buf_endian32(const void *data, enum endianness e);

void loop_endian16(uint16_t *data, enum endianness e, size_t cnt);

void loop_endian32(uint32_t *data, enum endianness e, size_t cnt);

void * memdup(const void *s, size_t n);

const void * memchk(const void *s, unsigned char c, size_t n);

bool grow_buffer(void *restrict ptr, size_t *alloc, size_t pos,
size_t elem_size);

void skip_line(FILE *ifp);

long file_get_remaining(FILE *ifp);

int munmap_stream(struct mmap_file mm);

struct mmap_file mmap_stream(FILE *ifp);

char * strerror_dup(int error);

char * id_template(const char *prefix, size_t num);

#endif /* COMMON_FUNCS */

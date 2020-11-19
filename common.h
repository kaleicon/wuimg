#ifndef COMMON_FUNCS
#define COMMON_FUNCS

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#define HIGHLIGHT "\x1b[7m"
#define RESET "\x1b[m"
#define CURSOR_UP "\x1b[A"
#define CURSOR_u_BACK "\x1b[%dD"
#define CLEAR_LINE "\x1b[K"
#define CLEAR_PREV_LINE CURSOR_UP CLEAR_LINE
#define ARRAY_LEN(arr) ( sizeof(arr) / sizeof(*arr) )

#ifndef MAP_FAILED
#define MAP_FAILED ((void *)-1)
#endif

enum endianness {
	big_endian = 0,
	little_endian = 1,
};

struct mmap_file {
	size_t len;
	unsigned char *data;
};

struct utc_time {
	int year, mon, day, hour, min, sec;
};

void rfc3339_format(time_t t, FILE *out);

time_t utc_to_epoch(const struct utc_time *tm);

long clock_nanodiff(const struct timespec *start);

void clock_start(struct timespec *start);

size_t scanline_length(size_t width, size_t bitdepth, size_t alignment);

int imod(int val, int max);

int iclamp(int n, int min, int max);

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

bool memchk(const void *s, int c, size_t n);

bool grow_buffer(void *restrict ptr, size_t *alloc, size_t pos,
size_t elem_size);

bool grow_string(char **str, size_t *alloc, size_t pos);

void skip_line(FILE *ifp);

void print_temp_line(const char *text);

size_t printable_len(const char *data, size_t len);

char * conv_unsafe_data(const void *restrict data, size_t len, size_t *outlen);

void print_unsafe_data(const char *name, const void *restrict data, size_t len,
FILE *stream);

int munmap_stream(struct mmap_file mm);

struct mmap_file mmap_stream(FILE *ifp);

char * id_template(const char *prefix, size_t n);

#endif /* COMMON_FUNCS */

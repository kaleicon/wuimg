#ifndef COMMON_FUNCS
#define COMMON_FUNCS

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#define HIGHLIGHT "\033[7m"
#define RESET "\033[m"
#define CURSOR_UP "\033[A"
#define CURSOR_u_BACK "\033[%dD"
#define CLEAR_LINE "\033[K"
#define CLEAR_PREV_LINE CURSOR_UP CLEAR_LINE
#define ARRAY_LEN(arr) ( sizeof(arr) / sizeof(*arr) )

enum endianness {
	big_endian = 0,
	little_endian = 1,
};

struct text_block {
	size_t tail;
	char buf[BUFSIZ];
};

void * flex_realloc(void *flex, size_t head, size_t nmemb, size_t size);

void * flex_calloc(size_t head, size_t nmemb, size_t size);

void * flex_malloc(size_t head, size_t nmemb, size_t size);

size_t read_spaced_text(struct text_block *text, FILE *ifp);

struct text_block * new_text_block(void);

long timespec_nanodiff(struct timespec before, struct timespec after);

size_t scanline_length(size_t width, size_t bitdepth, size_t alignment);

int fixed_point_scale(int outmax, int inmax, int prec);

int iwrap(int val, int max);

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

unsigned int integer_fit(unsigned int contain_w, unsigned int contain_h,
unsigned int fit_w, unsigned int fit_h);

enum endianness which_end(void);

uint32_t endian32(uint32_t val, enum endianness e);

uint16_t endian16(uint16_t val, enum endianness e);

uint16_t buf_endian16(const void *data, enum endianness e);

uint32_t buf_endian32(const void *data, enum endianness e);

void loop_endian16(uint16_t *data, enum endianness e, size_t cnt);

void loop_endian32(uint32_t *data, enum endianness e, size_t cnt);

void print_temp_line(const char *text);

void print_unsafe_data(const void *data, size_t len, const char *name,
const bool newline, FILE *stream);

unsigned char * read_file_to_mem(FILE *ifp, size_t *size);

char * id_template(const char *prefix, size_t n);

#endif /* COMMON_FUNCS */

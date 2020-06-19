#ifndef COMMON_FUNCS
#define COMMON_FUNCS

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

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

long timespec_nanodiff(const struct timespec *restrict before,
const struct timespec *restrict after);

int iwrapadd(int val, const int add, const int max);

int iclamp(const int n, const int min, const int max);

int imax(const int x, const int y);

int imin(const int x, const int y);

size_t zumax(const size_t x, const size_t y);

size_t zumin(const size_t x, const size_t y);

unsigned int umax(const unsigned int x, const unsigned int y);

unsigned int umin(const unsigned int x, const unsigned int y);

float fclampf(const float n, const float min, const float max);

size_t integer_fit(const size_t contain_w, const size_t contain_h,
const size_t fit_w, const size_t fit_h);

u_int16_t swap_u16(const u_int16_t val);

void swap_u16_inplace(void *data, const size_t cnt);

u_int16_t endian_u16(const void *data, const enum endianness e);

u_int32_t endian_u32(const void *data, const enum endianness e);

void loop_endian_u32(void *data, const enum endianness e, const size_t cnt);

void print_temp_line(const char *text);

void print_unsafe_data(const void *data, size_t len, const char *name,
const bool newline);

int print_error(const char *restrict action, const char *restrict file,
const char *restrict msg);

unsigned char * read_file_to_mem(FILE *ifp, size_t *size);

char * id_template(const char *prefix, const size_t n);

#endif /* COMMON_FUNCS */

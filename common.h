#ifndef COMMON_FUNCS
#define COMMON_FUNCS

#include <stdlib.h>

#include "wudefs.h"

enum endianness {
	big_endian = 0,
	little_endian = 1,
};

int iwrapadd(int val, const int add, const int max);

float fclampf(const float n, const float min, const float max);

int iclamp(const int n, const int min, const int max);

float fmaxf(const float x, const float y);

float fminf(const float x, const float y);

int imax(const int x, const int y);

int imin(const int x, const int y);

u_int16_t endian_uint16(const void *data, const enum endianness e);

u_int32_t endian_uint32(const void *data, const enum endianness e);

long timespec_nanodiff(const struct timespec *before,
const struct timespec *after);

int timespec_millidiff(const struct timespec *before,
const struct timespec *after);

void print_unsafe_data(const void *data, const size_t len);

int print_error(const char *restrict action, const char *restrict file,
const char *restrict msg);

unsigned char * read_file_to_mem(const char *filename, size_t *size);

char * id_template(const char *prefix, const size_t n);

#endif /* COMMON_FUNCS */

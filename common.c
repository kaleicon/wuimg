#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <unistd.h>

#include "wudefs.h"
#include "common.h"

int iwrapadd(int val, const int add, const int max) {
	val += add;
	if (val < 0) {
		return max + (val % max);
	}
	return val % max;
}

float fclampf(const float n, const float min, const float max) {
	if (n < min) {
		return min;
	} else if (n > max) {
		return max;
	}
	return n;
}

int iclamp(const int n, const int min, const int max) {
	if (n < min) {
		return min;
	} else if (n > max) {
		return max;
	}
	return n;
}

float fmaxf(const float x, const float y) {
	return x > y ? x : y;
}

float fminf(const float x, const float y) {
	return x < y ? x : y;
}

int imax(const int x, const int y) {
	return x > y ? x : y;
}

int imin(const int x, const int y) {
	return x < y ? x : y;
}

u_int16_t endian_uint16(const void *data, const enum endianness e) {
	const u_int8_t *d = (const u_int8_t *)data;
	switch (e) {
	case big_endian:
		return (u_int16_t)(d[0]<<8 | d[1]);
	default:
		return (u_int16_t)(d[1]<<8 | d[0]);
	}
}

u_int32_t endian_uint32(const void *data, const enum endianness e) {
	const u_int8_t *d = (const u_int8_t *)data;
	switch (e) {
	case big_endian:
		return (u_int32_t)(d[0]<<24 | d[1]<<16 | d[2]<<8 | d[3]);
	default:
		return (u_int32_t)(d[3]<<24 | d[2]<<16 | d[1]<<8 | d[0]);
	}
}

long timespec_nanodiff(const struct timespec *before,
const struct timespec *after) {
	return (after->tv_sec - before->tv_sec) * 1000000000
		+ after->tv_nsec - before->tv_nsec;
}

int timespec_millidiff(const struct timespec *before,
const struct timespec *after) {
	return (int)(timespec_nanodiff(before, after) / 100000);
}

#define HIGHLIGHT "\033[7m"
#define RESET "\033[m"
void print_unsafe_data(const void *data, const size_t len) {
	if (!len) {
		return;
	}
	const char *restrict d = (const char *restrict)data;
	if (isatty(fileno(stdout))) {
		for (size_t i = 0; i < len; ++i) {
			if (isprint(d[i]) || isspace(d[i])) {
				putchar(d[i]);
			} else {
				printf(HIGHLIGHT "<%.2hhX>" RESET, d[i]);
			}
		}
	} else {
		fwrite(data, 1, len, stdout);
	}
	putchar('\n');
}

unsigned char * read_file_to_mem(const char *filename, size_t *size) {
	unsigned char *buf = NULL;
	FILE *ifp = fopen(filename, "rb");
	if (ifp) {
		fseek(ifp, 0, SEEK_END);
		*size = (size_t)ftell(ifp);
		fseek(ifp, 0, SEEK_SET);
		buf = malloc(*size);
		if (buf) {
			fread(buf, 1, *size, ifp);
		}
		fclose(ifp);
	}
	return buf;
}

char * id_template(const char *prefix, const size_t n) {
	// One '+1' for NULL. I forgot what the other 1 was for but it works
	size_t len = 1 + strlen(prefix) + 1;
	for (size_t bound = 10; bound < n; bound *= 10) {
		++len;
	}
	char *restrict id = malloc(len);
	sprintf(id, "%s%zu", prefix, n);
	return id;
}

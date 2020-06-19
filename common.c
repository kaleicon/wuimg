#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>
#include <unistd.h>

#include "common.h"

long timespec_nanodiff(const struct timespec *restrict before,
const struct timespec *restrict after) {
	return (after->tv_sec - before->tv_sec) * 1000000000
		+ after->tv_nsec - before->tv_nsec;
}

int iwrapadd(int val, const int add, const int max) {
	val += add;
	if (val < 0) {
		return max + (val % max);
	}
	return val % max;
}

int iclamp(const int n, const int min, const int max) {
	if (n < min) {
		return min;
	} else if (n > max) {
		return max;
	}
	return n;
}

int imax(const int x, const int y) {
	return x > y ? x : y;
}

int imin(const int x, const int y) {
	return x < y ? x : y;
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

float fclampf(const float n, const float min, const float max) {
	if (n < min) {
		return min;
	} else if (n > max) {
		return max;
	}
	return n;
}

size_t integer_fit(const size_t contain_w, const size_t contain_h,
const size_t fit_w, const size_t fit_h) {
	const size_t wi = (fit_w + contain_w - 1) / contain_w;
	const size_t hi = (fit_h + contain_h - 1) / contain_h;
	return zumin(wi, hi);
}

u_int16_t swap_u16(const u_int16_t val) {
	return (u_int16_t)((val << 8) | (val >> 8));
}

void swap_u16_inplace(void *data, const size_t cnt) {
	u_int16_t *restrict d = data;
	for (size_t i = 0; i < cnt; ++i) {
		d[i] = swap_u16(d[i]);
	}
}

u_int16_t endian_u16(const void *data, const enum endianness e) {
	const u_int8_t *d = (const u_int8_t *)data;
	switch (e) {
	case big_endian:
		return (u_int16_t)(d[0]<<8 | d[1]);
	default:
		return (u_int16_t)(d[1]<<8 | d[0]);
	}
}

u_int32_t endian_u32(const void *data, const enum endianness e) {
	const u_int8_t *d = (const u_int8_t *)data;
	switch (e) {
	case big_endian:
		return (u_int32_t)(d[0]<<24 | d[1]<<16 | d[2]<<8 | d[3]);
	default:
		return (u_int32_t)(d[3]<<24 | d[2]<<16 | d[1]<<8 | d[0]);
	}
}

void loop_endian_u32(void *data, const enum endianness e, const size_t cnt) {
	u_int32_t *d = data;
	for (size_t i = 0; i < cnt; ++i) {
		d[i] = endian_u32(d + i, e);
	}
}

void print_temp_line(const char *text) {
	printf(CURSOR_u_BACK, printf(CLEAR_LINE "%s", text));
	fflush(stdout);
}

static size_t printable_len(const char *data, size_t len) {
	while (len) {
		const int c = data[len - 1];
		if (!isspace(c) && c != '\0') {
			break;
		}
		--len;
	}
	return len;
}

void print_unsafe_data(const void *data, size_t len, const char *name,
const bool newline) {
	const unsigned char *restrict d = (const unsigned char *restrict)data;
	if (isatty(fileno(stdout))) {
		len = printable_len(data, len);
		if (!len) {
			return;
		}
		if (name) {
			printf("%s: ", name);
		}

		for (size_t i = 0; i < len; ++i) {
			if (isprint(d[i]) || isspace(d[i])) {
				putchar(d[i]);
			} else {
				printf(HIGHLIGHT "x%.2hhx" RESET, d[i]);
			}
		}
	} else {
		if (!len) {
			return;
		}
		if (name) {
			printf("%s: ", name);
		}

		fwrite(data, 1, len, stdout);
	}
	if (newline) {
		putchar('\n');
	}
}

unsigned char * read_file_to_mem(FILE *ifp, size_t *size) {
	fseek(ifp, 0, SEEK_END);
	*size = (size_t)ftell(ifp);
	fseek(ifp, 0, SEEK_SET);
	unsigned char *buf = malloc(*size);
	if (buf) {
		fread(buf, 1, *size, ifp);
	}
	return buf;
}

char * id_template(const char *prefix, const size_t n) {
	size_t len = strlen(prefix) + 1 /* digit */ + 1 /* NULL */;
	for (size_t bound = 10; bound < n; bound *= 10) {
		++len;
	}
	char *restrict id = malloc(len);
	if (!id) {
		return NULL;
	}
	sprintf(id, "%s%zu", prefix, n);
	return id;
}

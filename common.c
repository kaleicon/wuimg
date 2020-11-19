#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>
#include <errno.h>
#include <math.h>

#include <sys/mman.h>

#include <iconv.h>
#include <uchardet/uchardet.h>

#include "common.h"

static const long DAYS_BETWEEN_1970_2000 = 10957;

void rfc3339_format(time_t t, FILE *out) {
	// Set our epoch to the first of March, 2000.
	// https://howardhinnant.github.io/date_algorithms.html
	const time_t to_era = (DAYS_BETWEEN_1970_2000 + 31 + 29) * 86400;

	t -= to_era;
	long days = t / 86400;
	long secs = t % 86400;
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

	years = years + 4*leaps + 100*centuries + 400*greg_cycles;

	const unsigned char month_days[] = {
		31 /* March */, 30, 31, 30, 31, 31,
		30,             31, 30, 31, 31, 29};
	long months = 0;
	while (month_days[months] <= days) {
		days -= month_days[months];
		++months;
	}


	years += 2000;
	months += 3;
	if (months >= 12) {
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

	time_t days_since_epoch;
	bool is_leap;
	{
		const int millenial_year = year - 2000;

		int greg_cycles = millenial_year / 400;
		int rem = millenial_year % 400;
		if (rem < 0) {
			--greg_cycles;
			rem += 400;
		}

		int centuries = rem / 100;
		rem -= centuries * 100;

		is_leap = (rem % 4 == 0);
		int leap_days = rem / 4;
		leap_days += 97 * greg_cycles + 24 * centuries - is_leap;

		days_since_epoch = millenial_year * 365 + leap_days;
		days_since_epoch += DAYS_BETWEEN_1970_2000;
	}

	const int days_in_this_year[] = {
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

long clock_nanodiff(const struct timespec *start) {
	struct timespec end;
	clock_gettime(CLOCK_REALTIME, &end);
	return (end.tv_sec - start->tv_sec) * 1000000000
		+ end.tv_nsec - start->tv_nsec;
}

void clock_start(struct timespec *start) {
	clock_gettime(CLOCK_REALTIME, start);
}

size_t scanline_length(const size_t width, const size_t bitdepth,
size_t alignment) {
	const size_t bytes = (width * bitdepth + 7) / 8;
	--alignment;
	return (bytes + alignment) & (~alignment);
}

int imod(int val, const int max) {
	return (val % max + max) % max;
}

int iclamp(const int n, const int min, const int max) {
	if (n < min) {
		return min;
	} else if (n > max) {
		return max;
	}
	return n;
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

float fclampf(const float n, const float min, const float max) {
	if (n < min) {
		return min;
	} else if (n > max) {
		return max;
	}
	return n;
}

enum endianness which_end(void) {
	union {
		uint16_t sh;
		uint8_t ch[2];
	} test = {.sh = 0x0001};
	if (test.ch[0]) {
		return little_endian;
	} else {
		return big_endian;
	}
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

uint16_t buf_endian16(const void *restrict data, const enum endianness e) {
	const uint8_t *restrict d = data;
	switch (e) {
	case big_endian:
		return (uint16_t)(d[0] << 8 | d[1]);
	default:
		return (uint16_t)(d[1] << 8 | d[0]);
	}
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

void loop_endian16(uint16_t *data, const enum endianness e, const size_t cnt) {
	for (size_t i = 0; i < cnt; ++i) {
		data[i] = endian16(data[i], e);
	}
}

void loop_endian32(uint32_t *data, const enum endianness e, const size_t cnt) {
	for (size_t i = 0; i < cnt; ++i) {
		data[i] = endian32(data[i], e);
	}
}

bool memchk(const void *s, const int c, const size_t n) {
	const unsigned char *ptr = s;
	for (size_t i = 0; i < n; ++i) {
		if (ptr[i] != c) {
			return false;
		}
	}
	return true;
}

bool grow_buffer(void *restrict ptr, size_t *alloc, const size_t pos,
const size_t elem_size) {
	if (pos >= *alloc) {
		const size_t new_len = zumax(pos, *alloc + *alloc / 4 + 1);
		void **var_loc = ptr;
		void *hold = realloc(*var_loc, elem_size * new_len);
		if (!hold) {
			return false;
		}
		*var_loc = hold;
		*alloc = new_len;
	}
	return true;
}

bool grow_string(char **str, size_t *alloc, const size_t pos) {
	return grow_buffer(str, alloc, pos + 1, sizeof(*str));
}

void skip_line(FILE *ifp) {
	int c;
	do {
		c = getc(ifp);
	} while (c != '\n' && c != EOF);
}

void print_temp_line(const char *text) {
	printf(CURSOR_u_BACK, printf(CLEAR_LINE "%s", text));
	fflush(stdout);
}

size_t printable_len(const char *data, size_t len) {
	while (len) {
		const int c = data[len - 1];
		if (c && !isspace(c)) {
			break;
		}
		--len;
	}
	return len;
}

static char * escape_data(const unsigned char *restrict data, const size_t len,
size_t *outlen) {
	size_t alloc = len;
	char *outbuf = malloc(alloc);
	if (!outbuf) {
		return NULL;
	}

	bool escaping = false;
	size_t pos = 0;
	const char hex[16] = "0123456789ABCDEF";
	for (size_t i = 0; i < len; ++i) {
		size_t fut_pos = pos + 1;
		const unsigned char c = data[i];
		if (isgraph(c) || isspace(c)) {
			if (escaping) {
				fut_pos += sizeof(RESET);
			}
			if (!grow_string(&outbuf, &alloc, fut_pos)) {
				free(outbuf);
				return NULL;
			}
			if (escaping) {
				memcpy(outbuf + pos, RESET, sizeof(RESET));
				pos += sizeof(RESET);
				escaping = false;
			}
			outbuf[pos] = (char)c;
			++pos;
		} else {
			char byte[] = {'x', hex[c >> 4], hex[c & 0x0f]};
			fut_pos += sizeof(byte);
			if (!escaping) {
				fut_pos += sizeof(HIGHLIGHT);
			}
			if (!grow_string(&outbuf, &alloc, fut_pos)) {
				free(outbuf);
				return NULL;
			}
			if (!escaping) {
				memcpy(outbuf + pos, HIGHLIGHT, sizeof(HIGHLIGHT));
				pos += sizeof(HIGHLIGHT);
				escaping = true;
			}
			memcpy(outbuf + pos, byte, sizeof(byte));
			pos += sizeof(byte);
		}
	}
	if (escaping) {
		if (!grow_string(&outbuf, &alloc, pos + sizeof(RESET)) ) {
			free(outbuf);
			return NULL;
		}
		memcpy(outbuf + pos, RESET, sizeof(RESET));
		pos += sizeof(RESET);
	}
	outbuf[pos] = 0;
	*outlen = pos;
	return outbuf;
}

static char * conv_iconv(const iconv_t cd, const void *restrict data,
size_t len, size_t *outlen) {
	char *outbuf = malloc(len);
	if (!outbuf) {
		return NULL;
	}

	size_t inleft = len;
	size_t outleft = len;
	char *inpos = (char *)data; // iconv insists on the input not being const
	char *outpos = outbuf;
	errno = 0;
	for (;;) {
		size_t n = iconv(cd, &inpos, &inleft, &outpos, &outleft);
		if (n == (size_t)-1) {
			if (errno == E2BIG) {
				const size_t add = 1 + len / 4;
				outleft += add;
				len += add;

				char *hold = realloc(outbuf, len);
				if (hold) {
					outpos = hold + len - outleft;
					outbuf = hold;
					errno = 0;
					continue;
				}
			}
			free(outbuf);
			return NULL;
		} else if (inleft == 0) { // Additional iter to flush output
			if (inpos) {
				inpos = NULL;
			} else {
				break;
			}
		}
	}
	*outlen = len - outleft;
	return outbuf;
}

char * conv_unsafe_data(const void *restrict data, const size_t len,
size_t *outlen) {
	if (len == 0) {
		*outlen = 0;
		return NULL;
	}

	const char *enc = "";
	uchardet_t ud = uchardet_new();
	const int error = uchardet_handle_data(ud, data, len);
	if (!error) {
		uchardet_data_end(ud);
		enc = uchardet_get_charset(ud);
	}

	/* What no one mentions is that deleting the context also deletes the
	 * charset string. */
	if (!enc[0]) {
		uchardet_delete(ud);
		return escape_data(data, len, outlen);
	} else if (!strcmp(enc, "ASCII") || !strcmp(enc, "UTF-8")) {
		uchardet_delete(ud);
		*outlen = len;
		char *str = malloc(*outlen);
		if (str) {
			memcpy(str, data, len);
		}
		return str;
	}

	const iconv_t cd = iconv_open("UTF-8", enc);
	uchardet_delete(ud);
	if (cd != (iconv_t)-1) {
		char *result = conv_iconv(cd, data, len, outlen);
		iconv_close(cd);
		if (result) {
			return result;
		}
	}
	return escape_data(data, len, outlen);
}

void print_unsafe_data(const char *name, const void *restrict data, size_t len,
FILE *stream) {
	char *d = escape_data(data, len, &len);
	if (d) {
		len = printable_len(d, len);

		if (name) {
			fputs(name, stream);
			fputs(": ", stream);
		}
		fwrite(d, 1, len, stream);
		putchar('\n');
		free(d);
	}
}

int munmap_stream(struct mmap_file mm) {
	return munmap(mm.data, mm.len);
}

struct mmap_file mmap_stream(FILE *ifp) {
	fseek(ifp, 0, SEEK_END);
	const size_t len = (size_t)ftell(ifp);
	return (struct mmap_file) {
		.len = len,
		.data = mmap(NULL, len, PROT_READ, MAP_SHARED, fileno(ifp), 0),
	};
}

char * id_template(const char *prefix, const size_t n) {
	size_t len = strlen(prefix) + 1 /* first digit */ + 1 /* NULL */;
	for (size_t bound = 10; bound < n; bound *= 10) {
		++len;
	}
	char *id = malloc(len);
	if (!id) {
		return NULL;
	}
	sprintf(id, "%s%zu", prefix, n);
	return id;
}

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>
#include <errno.h>

#include <iconv.h>
#include <uchardet/uchardet.h>

#include "common.h"

enum data_type {
	binary,
	utf8_text,
	conv_text,
};

void * flex_realloc(void *flex, const size_t head, const size_t nmemb,
const size_t size) {
	return realloc(flex, head + nmemb * size);
}

void * flex_calloc(const size_t head, const size_t nmemb, const size_t size) {
	return calloc(1, head + nmemb * size);
}

void * flex_malloc(const size_t head, const size_t nmemb, const size_t size) {
	return malloc(head + nmemb * size);
}

size_t read_spaced_text(struct text_block *text, FILE *ifp) {
	const size_t left = sizeof(text->buf) - text->tail;
	memcpy(text->buf, text->buf + left, text->tail);
	const size_t read = fread(text->buf + text->tail, 1, left, ifp);
	if (read == left) {
		size_t end = sizeof(text->buf);
		while (end && isgraph(text->buf[end - 1])) {
			--end;
		}
		text->tail = sizeof(text->buf) - end;
		while (end && isspace(text->buf[end - 1])) {
			--end;
		}

		return end;
	} else {
		text->buf[text->tail + read] = 0;
		text->tail = 0;
	}
	return read;
}

struct text_block * new_text_block(void) {
	struct text_block *b = malloc(sizeof(*b));
	if (b) {
		b->tail = 0;
		b->buf[sizeof(b->buf) - 1] = 0;
	}
	return b;
}

long timespec_nanodiff(const struct timespec before,
const struct timespec after) {
	return (after.tv_sec - before.tv_sec) * 1000000000
		+ after.tv_nsec - before.tv_nsec;
}

size_t scanline_length(const size_t width, const size_t bitdepth,
const size_t alignment) {
	const size_t bytes = (width * bitdepth + 7) / 8;
	return (bytes + alignment - 1) / alignment * alignment;
}

int fixed_point_scale(const int outmax, const int inmax, const int prec) {
	return (outmax << prec) / inmax + 1;
}

int iwrap(int val, const int max) {
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
	return x > y ? x : y;
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

unsigned int integer_fit(const unsigned contain_w, const unsigned contain_h,
const unsigned fit_w, const unsigned fit_h) {
	const unsigned int wi = (fit_w + contain_w - 1) / contain_w;
	const unsigned int hi = (fit_h + contain_h - 1) / contain_h;
	return umin(wi, hi);
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

void print_temp_line(const char *text) {
	printf(CURSOR_u_BACK, printf(CLEAR_LINE "%s", text));
	fflush(stdout);
}

static void print_escaped(const unsigned char *data, const size_t len, FILE* f) {
	size_t raw_start = 0;
	bool escaping = false;
	const char hex[16] = "0123456789ABCDEF";
	for (size_t i = 0; i < len; ++i) {
		const unsigned char c = data[i];
		if (isprint(c) || isspace(c)) {
			if (escaping) {
				escaping = false;
				raw_start = i;
				fputs(RESET, f);
			}
		} else {
			if (!escaping) {
				escaping = true;
				fwrite(data + raw_start, 1, i - raw_start, f);
				fputs(HIGHLIGHT, f);
			}
			const char byte[] = {'x', hex[c >> 4], hex[c & 0x0f]};
			fwrite(byte, 1, sizeof(byte), f);
		}
	}
	if (escaping) {
		fputs(RESET, f);
	} else {
		fwrite(data + raw_start, 1, len - raw_start, f);
	}
}

static size_t printable_len(const char *data, size_t len) {
	while (len) {
		const int c = data[len - 1];
		if (c && !isspace(c)) {
			break;
		}
		--len;
	}
	return len;
}

static char * conv_iconv(const iconv_t cd, const void *restrict data,
size_t *restrict len) {
	char *outbuf = malloc(*len);
	if (!outbuf) {
		return NULL;
	}

	size_t inleft = *len;
	size_t outleft = *len;
	char *inpos = (char *)data; // iconv insists on the input not being const
	char *outpos = outbuf;
	errno = 0;
	for (;;) {
		size_t n = iconv(cd, &inpos, &inleft, &outpos, &outleft);
		if (n == (size_t)-1) {
			if (errno == E2BIG) {
				outleft += *len;
				*len += *len;
				char *hold = realloc(outbuf, *len);
				if (hold) {
					outpos = hold + *len - outleft;
					outbuf = hold;
					errno = 0;
					continue;
				}
			}
			free(outbuf);
			outbuf = NULL;
			break;
		} else if (inleft == 0) { // Additional iteration to flush output
			if (inpos) {
				inpos = NULL;
			} else {
				*len -= outleft;
				break;
			}
		}
	}
	return outbuf;
}

static enum data_type conv_utf8(const char *restrict data, size_t *len,
const char **out) {
	*out = data;

	const char *enc = "";
	uchardet_t ud = uchardet_new();
	const int error = uchardet_handle_data(ud, data, *len);
	if (!error) {
		uchardet_data_end(ud);
		enc = uchardet_get_charset(ud);
	}

	/* What no one tells you is that deleting the context also deletes the
	 * charset string. */
	if (!enc[0]) {
		uchardet_delete(ud);
		return binary;
	} else if (!strcmp(enc, "ASCII") || !strcmp(enc, "UTF-8")) {
		uchardet_delete(ud);
		return utf8_text;
	}

	const iconv_t cd = iconv_open("UTF-8", enc);
	uchardet_delete(ud);
	if (cd == (iconv_t)-1) {
		return binary;
	}
	char *result = conv_iconv(cd, data, len);
	iconv_close(cd);
	if (result) {
		*out = result;
		return conv_text;
	}
	return binary;
}

void print_unsafe_data(const void *restrict data, size_t len,
const char *restrict name, const bool newline, FILE *stream) {
	const char *d;
	const enum data_type type = conv_utf8(data, &len, &d);
	if (type != binary) {
		len = printable_len(d, len);
	}

	if (len) {
		if (name) {
			fputs(name, stream);
			fputs(": ", stream);
		}

		if (type != binary) {
			fwrite(d, 1, len, stream);
		} else {
			print_escaped(data, len, stream);
		}
		if (newline) {
			fputc('\n', stream);
		}
	}

	if (type == conv_text) {
		free((char *)d);
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

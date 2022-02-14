#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>

#include "other.h"

static bool fill_buf(struct text_block *text, FILE *ifp, size_t *read) {
	const size_t rem = sizeof(text->buf) - text->tail;
	memcpy(text->buf, text->buf + rem, text->tail);
	*read = fread(text->buf + text->tail, 1, rem, ifp);
	return rem == *read;
}

size_t text_block_read_spaced(struct text_block *text, FILE *ifp) {
	size_t read;
	if (fill_buf(text, ifp, &read)) {
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

struct text_block * text_block_new(void) {
	struct text_block *b = malloc(sizeof(*b));
	if (b) {
		b->tail = 0;
		b->buf[sizeof(b->buf) - 1] = 0;
	}
	return b;
}

size_t text_read_uint(const unsigned char *restrict buf, mem_fast_t *val,
const size_t digits) {
	size_t i = 0;
	while (isspace(buf[i])) {
		++i;
	}

	*val = 0;
	for (size_t k = 0; k < digits && isdigit(buf[i]); ++k, ++i) {
		*val = *val * 10 + buf[i] - '0';
	}
	return i;
}

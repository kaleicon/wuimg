#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "text.h"

size_t read_delim_text(struct text_block *text, const char delim, FILE *ifp) {
	const size_t left = sizeof(text->buf) - text->tail;
	memcpy(text->buf, text->buf + left, text->tail);
	const size_t read = fread(text->buf + text->tail, 1, left, ifp);
	if (read == left) {
		size_t end = sizeof(text->buf);
		while (end && text->buf[end - 1] != delim) {
			--end;
		}
		text->tail = sizeof(text->buf) - end;
		return end;
	} else {
		text->buf[text->tail + read] = 0;
		text->tail = 0;
	}
	return read;
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
		if (!end) {
			return read_spaced_text(text, ifp);
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

int tonum(const int digit) {
	switch (digit) {
	case '0': return 0;
	case '1': return 1;
	case '2': return 2;
	case '3': return 3;
	case '4': return 4;
	case '5': return 5;
	case '6': return 6;
	case '7': return 7;
	case '8': return 8;
	case '9': return 9;
	}
	return -1;
}

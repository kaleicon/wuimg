#ifndef COMMON_TEXT
#define COMMON_TEXT

#include <stdio.h>

#include "memparser.h"

struct text_block {
	size_t tail;
	unsigned char buf[BUFSIZ];
};

size_t text_block_read_spaced(struct text_block *text, FILE *ifp);

struct text_block * text_block_new(void);

size_t text_read_uint(const unsigned char *restrict data, mem_fast_t *val,
size_t digits);

#endif /* COMMON_TEXT */

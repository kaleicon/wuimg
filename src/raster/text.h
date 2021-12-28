#ifndef COMMON_TEXT
#define COMMON_TEXT

#include <inttypes.h>

#include "../common.h"
#include "../wustr.h"

#define text_scan_xint_MACRO(tp, ptr) text_scan_xint( tp , sizeof(*( ptr )) , ptr )
#define text_scan_uint_MACRO(tp, ptr) text_scan_uint( tp , sizeof(*( ptr )) , ptr )

struct text_block {
	size_t tail;
	unsigned char buf[BUFSIZ];
};

struct text_parser {
	size_t pos;
	size_t len;
	const unsigned char *text;
};

typedef int_fast64_t text_fast_t;

size_t text_block_read_spaced(struct text_block *text, FILE *ifp);

struct text_block * text_block_new(void);

size_t text_read_uint(const unsigned char *restrict data, text_fast_t *val,
size_t digits);


unsigned char text_next_char_unsafe(struct text_parser *tp);

size_t text_get_uint_unsafe(struct text_parser *tp, size_t digits,
text_fast_t *val);

void text_skip_blank(struct text_parser *tp);

void text_skip_space(struct text_parser *tp);

void text_skip_nonspace(struct text_parser *tp);

void text_skip_line(struct text_parser *tp);

int text_next_char(struct text_parser *tp);

int text_next_nonblank(struct text_parser *tp);

int text_next_nonspace(struct text_parser *tp);

struct wuptr text_get_word(struct text_parser *tp);

size_t text_get_uint(struct text_parser *tp, size_t digits, text_fast_t *val);

bool text_scan_xint(struct text_parser *tp, size_t size, void *val);

bool text_scan_uint(struct text_parser *tp, size_t size, void *val);

struct text_parser text_parser_mem(size_t size, const void *data);

#endif /* COMMON_TEXT */

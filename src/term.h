#ifndef WU_TERM
#define WU_TERM

#include <stddef.h>

#include <termios.h>

struct term_restore {
	tcflag_t lflag;
	cc_t vmin;
	cc_t vtime;
};

void term_print_escaped(const uint8_t *restrict data, size_t len, bool is_utf8,
FILE *stream);

size_t term_event_read(unsigned char *output, size_t len);

void term_indent(size_t indent, FILE *out);

void term_line_put(const char *text, FILE *out);

void term_line_key_val(const char *key, const char *val, FILE *out);

void term_line_temp(const char *text);

void term_line_clear(void);

void term_noncanon_end(const struct term_restore *tr);

void term_noncanon_start(struct term_restore *tr);

#endif /* WU_TERM */

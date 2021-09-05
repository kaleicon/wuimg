#ifndef WU_TERM
#define WU_TERM

#include <stddef.h>

#include <termios.h>

struct term_restore {
	tcflag_t lflag;
	cc_t vmin;
	cc_t vtime;
};

size_t term_printable_len(const char *str, size_t len);

char * term_format_unsafe_data(const void *restrict data, size_t len,
size_t *outlen);

void term_print_unsafe_data(const char *name, const void *restrict data,
size_t len);

size_t term_event_read(unsigned char *output, size_t len);

void term_temp_line(const char *text);

void term_clear_line(void);

void term_noncanon_end(const struct term_restore *tr);

void term_noncanon_start(struct term_restore *tr);

#endif /* WU_TERM */

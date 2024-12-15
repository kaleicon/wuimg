// SPDX-License-Identifier: 0BSD
#include <ctype.h>
#include <string.h>

#include <unistd.h>

#include "term.h"
#include "misc/math.h"

void term_print_escaped(const unsigned char *restrict data, size_t len,
const bool is_utf8, FILE *stream) {
	const unsigned char hex[16] = "0123456789ABCDEF";
	const unsigned char HIGHLIGHT[] = {0x1b, '[', '7', 'm'};
	const unsigned char RESET[] = {0x1b, '[', 'm'};

	bool escaping = false;
	size_t region_start = 0;
	for (size_t i = 0; i < len; ++i) {
		const unsigned char c = data[i];
		if (isprint(c) || c == '\n' || c == '\t' || (!isascii(c) && is_utf8)
		|| (c == '\r' && i + 1 < len && data[i+1] == '\n')) {
			if (escaping) {
				fwrite(RESET, 1, sizeof(RESET), stream);
				region_start = i;
				escaping = false;
			}
		} else {
			if (!escaping) {
				fwrite(data + region_start, 1, i - region_start,
					stream);
				fwrite(HIGHLIGHT, 1, sizeof(HIGHLIGHT), stream);
				escaping = true;
			}
			unsigned char byte[] = {'x', hex[c >> 4], hex[c & 0x0f]};
			fwrite(byte, 1, sizeof(byte), stream);
		}
	}
	if (escaping) {
		fwrite(RESET, 1, sizeof(RESET), stream);
	} else {
		fwrite(data + region_start, 1, len - region_start, stream);
	}
}

static size_t char_run(struct term_queue *t, size_t i, uint8_t start,
uint8_t end) {
	size_t len = 0;
	while (i < t->used) {
		if (t->buf[i] < start || t->buf[i] > end) {
			break;
		}
		++i;
		++len;
	}
	return len;
}

unsigned char term_queue_next(struct term_queue *t) {
	if (t->used < sizeof(t->buf) / 2) {
		t->used += (size_t)read(STDIN_FILENO, t->buf + t->used,
			sizeof(t->buf) - t->used);
		if (!t->used) {
			return 0;
		}
	}

	unsigned char c = 0;
	size_t i = 0;
	const unsigned char esc_seq[] = {0x1b, '['};
	const unsigned char shift_mod[] = {'1', ';', '2'};
	if (t->buf[i] == esc_seq[0]) {
		++i;
		if (t->used > 2 && t->buf[i] == esc_seq[1]) {
			++i;

			const size_t params = char_run(t, i, 0x30, 0x3f);
			const bool shift = params == sizeof(shift_mod)
				&& !memcmp(t->buf + i, shift_mod, sizeof(shift_mod));
			i += params;

			const size_t middle = char_run(t, i, 0x20, 0x2f);
			i += middle;
			if (!middle && i < t->used) {
				switch (t->buf[i]) {
				case 'A': c = shift ? 'K' : 'k'; break;
				case 'B': c = shift ? 'J' : 'j'; break;
				case 'C': c = shift ? 'L' : 'l'; break;
				case 'D': c = shift ? 'H' : 'h'; break;
				case 'F': c = '='; break;
				case 'H': c = shift ? '1' : '0'; break;
				}
			}
		}
	} else {
		c = t->buf[i];
	}
	i = zumin(i + 1, t->used);

	memmove(t->buf, t->buf + i, t->used - i);
	t->used -= i;
	return c;
}

void term_indent(size_t indent, FILE *out) {
	const unsigned char spaces[] = {' ', ' ', ' ', ' '};
	if (indent > sizeof(spaces)) {
		if (isatty(fileno(out))) {
			fprintf(out, "\x1b[%zuC", indent);
			return;
		}
		do {
			fwrite(spaces, 1, sizeof(spaces), out);
			indent -= sizeof(spaces);
		} while (indent > sizeof(spaces));
	}
	fwrite(spaces, 1, indent, out);
}

void term_line_put(const char *text, FILE *out) {
	fputs(text, out);
	fputc('\n', out);
}

void term_line_key_val(const char *key, const char *val, FILE *out) {
	fputs(key, out);
	fputs(": ", out);
	fputs(val, out);
	fputc('\n', out);
}

static const char CLEAR_LINE[] = "\x1b[K";
void term_line_temp(const char *text) {
	fputs(CLEAR_LINE, stdout);
	fputs(text, stdout);
	fputc('\r', stdout);
	fflush(stdout);
}

void term_line_clear(void) {
	fputs(CLEAR_LINE, stdout);
	fflush(stdout);
}

void term_noncanon_end(const struct term_restore *tr) {
	if (isatty(STDIN_FILENO)) {
		struct termios term;
		tcgetattr(STDIN_FILENO, &term);

		term.c_lflag = tr->lflag;
		term.c_cc[VMIN] = tr->vmin;
		term.c_cc[VTIME] = tr->vtime;
		tcsetattr(STDIN_FILENO, TCSANOW, &term);
	}
}

void term_noncanon_start(struct term_restore *tr) {
	if (isatty(STDIN_FILENO)) {
		struct termios term;
		tcgetattr(STDIN_FILENO, &term);
		*tr = (struct term_restore) {
			.lflag = term.c_lflag,
			.vmin = term.c_cc[VMIN],
			.vtime = term.c_cc[VTIME],
		};

		term.c_lflag &= (tcflag_t)~(ECHO | ICANON);
		term.c_cc[VMIN] = 0;
		term.c_cc[VTIME] = 0;
		tcsetattr(STDIN_FILENO, TCSANOW, &term);
	}
}

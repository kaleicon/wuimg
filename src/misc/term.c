// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2021 kaleido
#include <ctype.h>
#include <limits.h>
#include <string.h>

#include <unistd.h>

#include "misc/math.h"
#include "misc/term.h"
#include "misc/utf8.h"
#include "misc/wustr.h"

void term_print_escaped(const unsigned char *restrict data, size_t len,
const bool is_utf8, FILE *out) {
	const unsigned char hex[16] = {
		'0', '1', '2', '3', '4', '5', '6', '7',
		'8', '9', 'A', 'B', 'C', 'D', 'E', 'F',
	};
	const unsigned char HIGHLIGHT[] = {0x1b, '[', '7', 'm'};
	const unsigned char RESET[] = {0x1b, '[', 'm'};

	bool escaping = false;
	size_t region_start = 0;
	const int pass_above = is_utf8 ? 0x80 : UCHAR_MAX + 1;
	for (size_t i = 0; i < len; ++i) {
		const unsigned char c = data[i];
		if (isprint(c) || c == '\n' || c == '\t' || (c >= pass_above)
		|| (c == '\r' && i + 1 < len && data[i+1] == '\n')) {
			if (escaping) {
				fwrite(RESET, 1, sizeof(RESET), out);
				region_start = i;
				escaping = false;
			}
		} else {
			if (!escaping) {
				fwrite(data + region_start, 1, i - region_start,
					out);
				fwrite(HIGHLIGHT, 1, sizeof(HIGHLIGHT), out);
				escaping = true;
			}
			unsigned char byte[] = {'x', hex[c >> 4], hex[c & 0x0f]};
			fwrite(byte, 1, sizeof(byte), out);
		}
	}
	if (escaping) {
		fwrite(RESET, 1, sizeof(RESET), out);
	} else {
		fwrite(data + region_start, 1, len - region_start, out);
	}
}

void term_print_convert(const char *text, FILE *out) {
	bool is_utf8 = false;
	size_t len = strlen(text);
	struct wustr conv;
	switch (utf8_convert(text, len, &conv, NULL)) {
	case trit_false: is_utf8 = true; break;
	case trit_true:
		is_utf8 = true;
		text = (const char *)conv.str;
		len = conv.len;
		break;
	case trit_what: break;
	}
	term_print_escaped((const unsigned char *)text, len, is_utf8, out);
	wustr_free(&conv);
}

static uint8_t char_run(struct term_queue *t, uint8_t i, uint8_t start,
uint8_t end) {
	uint8_t init = i;
	while (i < t->used && t->buf[i] >= start && t->buf[i] <= end) {
		++i;
	}
	return i - init;
}

unsigned char term_queue_next(struct term_queue *t) {
	if (t->used < sizeof(t->buf) / 2) {
		const ssize_t r = read(STDIN_FILENO, t->buf + t->used,
			sizeof(t->buf) - t->used);
		t->used = (uint8_t)(t->used + (r < 0 ? 0 : r));
		if (!t->used) {
			return 0;
		}
	}

	const unsigned char esc_seq[] = {0x1b, '['};
	const unsigned char shift_mod[] = {'1', ';', '2'};
	uint8_t c = 0;
	uint8_t i = 0;
	if (t->buf[i] == esc_seq[0]) {
		++i;
		if (t->used > 1 && t->buf[i] == esc_seq[1]) {
			++i;

			const uint8_t params_len = char_run(t, i, 0x30, 0x3f);
			const bool shift = params_len == sizeof(shift_mod)
				&& !memcmp(t->buf + i, shift_mod, sizeof(shift_mod));
			i += params_len;

			const uint8_t middle_len = char_run(t, i, 0x20, 0x2f);
			i += middle_len;
			if (!middle_len && i < t->used) {
				switch (t->buf[i]) {
				case 'A': c = shift ? 'K' : 'k'; break;
				case 'B': c = shift ? 'J' : 'j'; break;
				case 'C': c = shift ? 'L' : 'l'; break;
				case 'D': c = shift ? 'H' : 'h'; break;
				case 'F': c = '='; break;
				case 'H': c = shift ? '1' : '0'; break;
				}
				++i;
			}
		}
	} else {
		c = t->buf[i];
		++i;
	}

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

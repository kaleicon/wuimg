#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <unistd.h>

#include <termios.h>

#include <uchardet/uchardet.h>
#include <unicode/ucnv.h>

#include "common.h"
#include "term.h"
#include "wustr.h"

static size_t graph_len(const unsigned char *str, size_t len) {
	while (len) {
		const unsigned char c = str[len - 1];
		if (c && isgraph(c)) {
			break;
		}
		--len;
	}
	return len;
}

static void print_escaped(const unsigned char *restrict data, size_t len,
FILE *stream, const bool utf8) {
	len = graph_len(data, len);
	const unsigned char hex[16] = "0123456789ABCDEF";
	const unsigned char HIGHLIGHT[] = {0x1b, '[', '7', 'm'};
	const unsigned char RESET[] = {0x1b, '[', 'm'};

	bool escaping = false;
	size_t region_start = 0;
	for (size_t i = 0; i < len; ++i) {
		const unsigned char c = data[i];
		if (isprint(c) || c == '\n' || c == '\t' || (!isascii(c) && utf8)
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

static char * convert_str(UConverter *from, UConverter *to, const char *data,
const size_t len, size_t *outlen, UErrorCode *err) {
	*outlen = (size_t)UCNV_GET_MAX_BYTES_FOR_STRING(len, ucnv_getMaxCharSize(to));
	char *out = malloc(*outlen);
	if (out) {
		char *pos = out;
		ucnv_convertEx(to, from, &pos, pos + *outlen, &data, data + len,
			NULL, NULL, NULL, NULL, false, true, err);
		*outlen = (size_t)(pos - out);
		if (U_FAILURE(*err)) {
			free(out);
			return NULL;
		}
	}
	return out;
}

static char * detect_and_convert(const char *restrict data, const size_t len,
size_t *outlen, bool *is_utf8) {
	if (!len) {
		return NULL;
	}

	UErrorCode err;
	UConverter *from = NULL;
	uchardet_t ud = uchardet_new();
	if (ud) {
		const int error = uchardet_handle_data(ud, data, len);
		if (!error) {
			uchardet_data_end(ud);
			const char *enc = uchardet_get_charset(ud);
			/* No one mentions that deleting the context also
			 * invalidates the charset string. */
			if (enc[0]) {
				if (!strcmp(enc, "ASCII") || !strcmp(enc, "UTF-8")) {
					*is_utf8 = true;
				} else {
					from = ucnv_open(enc, &err);
				}
			}
		}
		uchardet_delete(ud);
	}

	char *result = NULL;
	if (from) {
		UConverter *to = ucnv_open("UTF-8", &err);
		if (to) {
			result = convert_str(from, to, data, len, outlen, &err);
			if (result) {
				*is_utf8 = true;
			}
			ucnv_close(to);
		}
		ucnv_close(from);
	}
	return result;
}

void term_print_unsafe(const void *restrict data, size_t len, FILE *stream) {
	bool is_utf8 = false;
	const void *out;
	char *conv = detect_and_convert(data, len, &len, &is_utf8);
	if (conv) {
		out = conv;
	} else {
		out = data;
	}
	print_escaped(out, len, stream, is_utf8);
	free(conv);
}

size_t term_event_read(unsigned char *output, const size_t len) {
	const unsigned char esc_seq[] = {0x1b, '['};
	const unsigned char shift_mod[] = {'1', ';', '2'};

	const ssize_t r = read(STDIN_FILENO, output, len);
	if (r < 1) {
		return 0;
	}

	const size_t read = (size_t)r;
	size_t i = 0;
	size_t written = 0;
	while (i < read) {
		if (output[i] == esc_seq[0]) {
			// No idea how all sequences end, so bail out if
			// unknown.
			if (i + 1 >= read && output[i+1] != esc_seq[1]) {
				break;
			}
			i += sizeof(esc_seq);

			bool shift = false;
			if (i + sizeof(shift_mod) < read
			&& !memcmp(output + i, shift_mod, sizeof(shift_mod)) ) {
				shift = true;
				i += sizeof(shift_mod);
			}

			unsigned char c;
			switch (output[i]) {
			case 'A': c = shift ? 'K' : 'k'; break;
			case 'B': c = shift ? 'J' : 'j'; break;
			case 'C': c = shift ? 'L' : 'l'; break;
			case 'D': c = shift ? 'H' : 'h'; break;
			case 'F': c = '='; break;
			case 'H': c = shift ? '1' : '0'; break;
			default: return written;
			}
			output[written] = c;
		} else if (isprint(output[i])) {
			output[written] = output[i];
		} else {
			break;
		}
		++i;
		++written;
	}
	return written;
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

const char CLEAR_LINE[] = "\x1b[K";
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

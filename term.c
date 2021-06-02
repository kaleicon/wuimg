#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <unistd.h>

#include <termios.h>
#include <iconv.h>

#include <uchardet/uchardet.h>

#include "common.h"
#include "term.h"

size_t term_printable_len(const char *str, size_t len) {
	while (len) {
		const unsigned char c = (unsigned char)str[len - 1];
		if (c && isgraph(c)) {
			break;
		}
		--len;
	}
	return len;
}

static char * escape_data(const unsigned char *restrict data,
const size_t len, size_t *outlen) {
	size_t alloc = len;
	char *out = malloc(alloc);
	if (!out) {
		return NULL;
	}

	bool escaping = false;
	size_t pos = 0;
	const char hex[16] = "0123456789ABCDEF";
	const char HIGHLIGHT[] = {0x1b, '[', '7', 'm'}; //"\x1b[7m";
	const char RESET[] = {0x1b, '[', 'm'}; //"\x1b[m";
	for (size_t i = 0; i < len; ++i) {
		const unsigned char c = data[i];
		size_t fut_pos = pos + 1;
		if (isgraph(c) || isspace(c)) {
			if (escaping) {
				fut_pos += sizeof(RESET);
			}
			if (!grow_buffer(&out, &alloc, fut_pos + 1 /*null*/, 1)) {
				free(out);
				return NULL;
			}
			if (escaping) {
				memcpy(out + pos, RESET, sizeof(RESET));
				pos = fut_pos - 1;
				escaping = false;
			}
			out[pos] = (char)c;
			++pos;
		} else {
			char byte[] = {'x', hex[c >> 4], hex[c & 0x0f]};
			fut_pos += sizeof(byte);
			if (escaping) {
				fut_pos += sizeof(HIGHLIGHT);
			}
			if (!grow_buffer(&out, &alloc, fut_pos + 1, 1)) {
				free(out);
				return NULL;
			}
			if (escaping) {
				memcpy(out + pos, HIGHLIGHT, sizeof(HIGHLIGHT));
				pos = fut_pos - 1;
				escaping = false;
			}
			memcpy(out + pos, byte, sizeof(byte));
			pos += sizeof(byte);
		}
	}
	if (escaping) {
		const size_t last = sizeof(RESET) - 1;
		if (!grow_buffer(&out, &alloc, pos + last + 1, 1)) {
			free(out);
			return NULL;
		}
		memcpy(out + pos, RESET, last);
		pos += last;
	}
	out[pos] = 0;
	*outlen = pos;
	return out;
}

static char * conv_iconv(const iconv_t cd, const void *restrict data,
const size_t len, size_t *outlen) {
	size_t alloc = len;
	char *out = malloc(alloc);
	if (!out) {
		return NULL;
	}

	size_t inleft = len;
	size_t outleft = len;
	char *inpos = (char *)data; // iconv insists on the input not being const
	char *outpos = out;
	errno = 0;
	for (;;) {
		size_t n = iconv(cd, &inpos, &inleft, &outpos, &outleft);
		if (n == (size_t)-1) {
			if (errno == E2BIG) {
				const size_t add = alloc / 4 + 1;
				outleft += add;
				alloc += add;

				char *hold = realloc(out, alloc);
				if (hold) {
					out = hold;
					outpos = out + alloc - outleft;
					errno = 0;
					continue;
				}
			}
			free(out);
			return NULL;
		} else if (inleft == 0) {
			if (inpos) { // Additional iter to flush output
				inpos = NULL;
			} else {
				break;
			}
		}
	}
	*outlen = alloc - outleft;
	out[*outlen] = 0;
	return out;
}

char * term_format_unsafe_data(const void *restrict data, const size_t len,
size_t *outlen) {
	*outlen = len;
	if (len == 0) {
		return NULL;
	}

	const char *enc = "";
	uchardet_t ud = uchardet_new();
	const int error = uchardet_handle_data(ud, data, len);
	if (!error) {
		uchardet_data_end(ud);
		enc = uchardet_get_charset(ud);
	}

	/* What no one mentions is that deleting the context also invalidates
	 * the charset string. */
	if (!enc[0]) {
		uchardet_delete(ud);
		return escape_data(data, len, outlen);
	} else if (!strcmp(enc, "ASCII") || !strcmp(enc, "UTF-8")) {
		uchardet_delete(ud);
		return memdup(data, len);
	}

	const iconv_t cd = iconv_open("UTF-8", enc);
	uchardet_delete(ud);
	if (cd != (iconv_t)-1) {
		char *result = conv_iconv(cd, data, len, outlen);
		iconv_close(cd);
		if (result) {
			return result;
		}
	}
	return escape_data(data, len, outlen);
}

void term_print_unsafe_data(const char *name, const void *restrict data,
size_t len) {
	char *out = escape_data(data, len, &len);
	if (out) {
		len = term_printable_len(out, len);

		if (name) {
			fputs(name, stdout);
			fputs(": ", stdout);
		}
		fwrite(out, 1, len, stdout);
		fputc('\n', stdout);
		free(out);
	}
}

size_t term_filter_read(unsigned char *output) {
	const unsigned char esc_seq[] = {0x1b, '['};
	const unsigned char shift_mod[] = {'1', ';', '2'};

	unsigned char ch[20] = {0};
	const ssize_t r = read(STDIN_FILENO, ch, sizeof(ch) - sizeof(shift_mod) - 1);
	if (r < 0) {
		return 0;
	}

	const size_t total = (size_t)r;
	size_t i = 0;
	size_t written = 0;
	while (i < total) {
		if (ch[i] == esc_seq[0]) {
			// No idea how all sequences end, so bail out if
			// unknown.
			if (ch[i+1] != esc_seq[1]) {
				break;
			}
			i += sizeof(esc_seq);

			bool shift = false;
			if (!memcmp(ch + i, shift_mod, sizeof(shift_mod)) ) {
				shift = true;
				i += sizeof(shift_mod);
			}

			unsigned char c;
			switch (ch[i]) {
			case 'A': c = (shift ? 'K' : 'k'); break;
			case 'B': c = (shift ? 'J' : 'j'); break;
			case 'C': c = (shift ? 'L' : 'l'); break;
			case 'D': c = (shift ? 'H' : 'h'); break;
			case 'F': c = '1'; break;
			case 'H': c = '0'; break;
			default: return written;
			}
			output[written] = c;
		} else if (isprint(ch[i])) {
			output[written] = ch[i];
		} else {
			break;
		}
		++i;
		++written;
	}
	return written;
}

const char CLEAR_LINE[] = "\x1b[K";
void term_temp_line(const char *text) {
	fputs(CLEAR_LINE, stdout);
	fputs(text, stdout);
	fputc('\r', stdout);
	fflush(stdout);
}

void term_clear_line(void) {
	fputs(CLEAR_LINE, stdout);
	fflush(stdout);
}

void term_noncanon_end(const struct term_restore *tr) {
	struct termios term;
	tcgetattr(STDIN_FILENO, &term);

	term.c_lflag = tr->lflag;
	term.c_cc[VMIN] = tr->vmin;
	term.c_cc[VTIME] = tr->vtime;
	tcsetattr(STDIN_FILENO, TCSANOW, &term);
}

void term_noncanon_start(struct term_restore *tr) {
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

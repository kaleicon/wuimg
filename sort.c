#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#include "common.h"
#include "sort.h"

void natfrm_free(struct natfrm *frm) {
	for (size_t i = 0;; ++i) {
		if (frm[i].is_str) {
			free(frm[i].v.str);
		}
		if (!frm[i].more) {
			break;
		}
	}
	free(frm);
}

int natcmp(const struct natfrm *restrict xx, const struct natfrm *restrict yy) {
	size_t i = 0;
	while (xx[i].is_str == yy[i].is_str) {
		int diff;
		if (xx[i].is_str) {
			diff = memcmp(xx[i].v.str, yy[i].v.str,
				umin(xx[i].n.str_len, yy[i].n.str_len));
			if (diff) {
				return diff;
			}
		} else {
			diff = xx[i].n.zeroes - yy[i].n.zeroes;
			if (diff) {
				return diff;
			}
			if (xx[i].v.num > yy[i].v.num) {
				return 1;
			} else if (xx[i].v.num < yy[i].v.num) {
				return -1;
			}
		}
		diff = xx[i].more + yy[i].more;
		switch (diff) {
		case 0: return diff;
		case 1: return xx[i].more - yy[i].more;
		default:
			++i;
		}
	}
	return xx[i].is_str - yy[i].is_str;
}

struct natfrm * natfrm_str(char *str, struct natfrm_data *data) {
	size_t alloc = data->max_alloc_seen;
	struct natfrm *out = malloc(sizeof(*out) * alloc);
	if (!out) {
		return NULL;
	}

	size_t i = 0;
	size_t o = 0;
	for (;;) {
		if (!grow_buffer(&out, &alloc, o, sizeof(*out))) {
			out[o].more = false;
			natfrm_free(out);
			return NULL;
		}

		struct natfrm *pos = out + o;
		if (isdigit(str[i])) {
			pos->is_str = false;
			pos->n.zeroes = ~0;
			pos->v.num = 0;
			while (str[i] == '0') {
				--pos->n.zeroes;
				++i;
			}
			while (isdigit(str[i])) {
				pos->v.num = pos->v.num * 10 + (str[i] - '0');
				++i;
			}
		} else {
			pos->is_str = true;
			const size_t start = i;
			while (!isdigit(str[i]) && str[i]) {
				++i;
			}

			const char c = str[i];
			str[i] = 0;
			const size_t xlen = strxfrm(data->buf, str + start,
				data->buf_len) + 1;
			str[i] = c;
			pos->v.str = malloc(xlen);
			if (!pos->v.str) {
				pos->more = false;
				natfrm_free(out);
				return NULL;
			}
			memcpy(pos->v.str, data->buf, xlen);
			pos->n.str_len = (unsigned int)xlen;
		}

		if (!str[i]) {
			break;
		}
		pos->more = true;
		++o;
	}
	out[o].more = false;
	if (o + 1 > data->max_alloc_seen) {
		data->max_alloc_seen = o + 1;
	}
	return out;
}

void natfrm_data_free(struct natfrm_data *data) {
	free(data->buf);
}

bool natfrm_data_init(struct natfrm_data *data, size_t max_str_len) {
	const size_t len = (max_str_len + 1) * 4;
	*data = (struct natfrm_data) {
		.max_alloc_seen = 4,
		.buf_len = len,
		.buf = malloc(len),
	};
	return (bool)data->buf;
}

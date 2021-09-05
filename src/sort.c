#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#include "common.h"
#include "sort.h"

static bool fast_digit_or_null(const unsigned char c) {
	const unsigned char remain = c % 16;
	switch (c / 16) {
	case 0: return remain == 0;
	case 3: return remain < 10;
	}
	return false;
}

void natfrm_free(struct natfrm *frm) {
	free(frm->buf);
	free(frm);
}

__attribute__((unused))
static void num_print(const unsigned char *num, const int len) {
	for (int i = 0; i < len/2; ++i) {
		unsigned char buf[2];
		buf[0] = (num[i] >> 4) + '0';
		buf[1] = (num[i] & 0x0f) + '0';
		fwrite(buf, 1, sizeof(buf), stdout);
	}
	putchar('\n');
}

int natcmp(const struct natfrm *restrict xx, const struct natfrm *restrict yy) {
	if (xx->str_first == yy->str_first) {
		bool is_str = xx->str_first;
		const int iters = imin(xx->len, yy->len);
		for (int i = 0; i < iters; ++i) {
			const struct natfrm_item *x = xx->nat + i;
			const struct natfrm_item *y = yy->nat + i;

			int diff;
			size_t len;
			if (is_str) {
				len = (size_t)imin(x->len, y->len);
			} else {
				diff = x->zeroes - y->zeroes;
				if (diff) {
					return diff;
				}
				diff = x->len - y->len;
				if (diff) {
					return diff;
				}
				len = (size_t)x->len / 2;
			}
			diff = memcmp(x->str, y->str, len);
			if (diff) {
				return diff;
			}
			is_str = !is_str;
		}
		return xx->len - yy->len;
	}
	return xx->str_first - yy->str_first;
}

struct natfrm * natfrm_str(char *str_char, struct natfrm_data *data) {
	struct natfrm *out = malloc(sizeof(*out) + sizeof(*out->nat) * data->alloc);
	if (!out) {
		return NULL;
	}

	out->buf = NULL;
	if (data->pool_pos > data->pool_limit) {
		data->pool = malloc(data->pool_size);
		if (!data->pool) {
			natfrm_free(out);
			return NULL;
		}
		data->pool_pos = 0;
		out->buf = data->pool;
	}

	int i = 0;
	size_t o = 0;
	unsigned char *str = (unsigned char *)str_char;
	bool digit = isdigit(str[i]);
	out->str_first = !digit;
	for (;;) {
		if (digit) {
			int start = i;
			while (str[i] == '0') {
				++i;
			}
			out->nat[o].zeroes = start - i;

			out->nat[o].str = data->pool + data->pool_pos;
			start = i;
			while (isdigit(str[i])) {
				unsigned char c = str[i] << 4;
				++i;
				if (isdigit(str[i])) {
					c |= (unsigned char)(str[i] - ('0' - 1));
					++i;
				}
				data->pool[data->pool_pos] = c;
				++data->pool_pos;
			}
			out->nat[o].len = (i - start + 1);

		} else {
			const int start = i;
			do {
				++i;
			} while (!fast_digit_or_null(str[i]));
			const unsigned char c = str[i];
			str[i] = 0;

			void *xfrm = data->pool + data->pool_pos;
			const size_t xlen = strxfrm(xfrm, (char *)str + start,
				(size_t)(i ) * 4);
			out->nat[o].str = xfrm;
			out->nat[o].len = (int)xlen;
			data->pool_pos += xlen;
			str[i] = c;
		}

		++o;
		if (!str[i]) {
			out->len = (int)o;
			break;
		}
		if (o >= data->alloc) {
			data->alloc += data->alloc/4;
			void *hold = realloc(out,
				sizeof(*out) + sizeof(*out->nat) * data->alloc);
			if (!hold) {
				natfrm_free(out);
				return NULL;
			}
			out = hold;
		}
		digit = !digit;
	}
	return out;
}

void natfrm_data_init(struct natfrm_data *data, size_t max_str_len) {
	const size_t min = max_str_len * 4;
	const size_t size = min * 64;
	*data = (struct natfrm_data) {
		.alloc = 4,
		.pool_limit = size - min,
		.pool_size = size,
		.pool_pos = size,
	};
}

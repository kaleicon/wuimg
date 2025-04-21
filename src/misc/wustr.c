// SPDX-License-Identifier: 0BSD
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "wustr.h"
#include "misc/common.h"
#include "misc/math.h"

bool wugrow_reserve(struct wugrow *grow, size_t extra) {
	const size_t needed = grow->pos + extra;
	if (needed >= grow->alloc) {
		const size_t new_len = zumax(needed,
			grow->alloc + grow->alloc / 4) + 16;
		void *hold = small_realloc(grow->ptr, new_len, grow->elem_size);
		if (!hold) {
			return false;
		}
		grow->ptr = hold;
		grow->alloc = new_len;
	}
	return true;
}

bool wugrow_recheck(struct wugrow *grow) {
	return wugrow_reserve(grow, 0);
}

struct wugrow wugrow_init(const size_t elem_size) {
	return (struct wugrow) {
		.elem_size = elem_size,
	};
}


struct wuptr wuptr_mem(const void *mem, const size_t len) {
	return (struct wuptr){.len = len, .ptr = mem};
}

struct wuptr wuptr_str(const char *str) {
	return wuptr_mem(str, strlen(str));
}

struct wuptr wuptr_wustr(const struct wustr w) {
	return (struct wuptr){.len = w.len, .ptr = w.str};
}

struct wuptr wuptr_trim_end(struct wuptr w, const unsigned char c) {
	while (w.len && w.ptr[w.len-1] == c) {
		--w.len;
	}
	return w;
}

bool wuptr_suffix(const struct wuptr w1, const struct wuptr w2) {
	if (w1.len >= w2.len) {
		const size_t diff = w1.len - w2.len;
		return !memcmp(w1.ptr + diff, w2.ptr, w2.len);
	}
	return false;
}

bool wuptr_suffix_str(const struct wuptr w1, const char *s2) {
	return wuptr_suffix(w1, wuptr_str(s2));
}

bool wuptr_eq(const struct wuptr w1, const struct wuptr w2) {
	if (w1.len == w2.len) {
		return !memcmp(w1.ptr, w2.ptr, w1.len);
	}
	return false;
}

bool wuptr_eq_str(const struct wuptr w1, const char *s2) {
	return wuptr_eq(w1, wuptr_str(s2));
}

size_t wuptr_print(const struct wuptr w, FILE *out) {
	return fwrite(w.ptr, 1, w.len, out);
}


void wustr_free(struct wustr *w) {
	free(w->str);
}

bool wustr_realloc(struct wustr *w, const size_t len) {
	void *hold = small_realloc(w->str, len + 1, 1);
	if (hold) {
		w->len = len;
		w->str = hold;
	}
	return (bool)hold;
}

bool wustr_malloc(struct wustr *w, const size_t len) {
	w->len = len;
	w->str = small_malloc(len + 1, 1);
	return (bool)w->str;
}

bool wustr_memdup(struct wustr *w, const char *str, const size_t len) {
	w->str = malloc(len + 1);
	if (w->str) {
		w->len = len;
		memcpy(w->str, str, len);
		w->str[len] = 0;
	}
	return (bool)w->str;
}

bool wustr_append_line(struct wustr *w, const char *str,
const bool strip_trailing_spaces) {
	str = str ? str : "(null)";
	size_t len = strlen(str);
	if (strip_trailing_spaces) {
		while (len && isspace(str[len-1])) {
			--len;
		}
	}
	const size_t oldlen = w->len;
	const size_t newlen = len + oldlen + 1 /* newline */;
	if (wustr_realloc(w, newlen)) {
		memcpy(w->str + oldlen, str, len);
		w->str[newlen-1] = '\n';
		w->str[newlen] = 0;
		return true;
	}
	return false;
}

size_t wustr_print(const struct wustr *w, FILE *out) {
	return wuptr_print(wuptr_wustr(*w), out);
}

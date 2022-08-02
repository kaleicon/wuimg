#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "wustr.h"
#include "common.h"

bool wugrow_reserve(void *restrict ptr_ptr, struct wugrow *grow, size_t extra) {
	const size_t needed = grow->pos + extra;
	if (needed >= grow->alloc) {
		const size_t new_len = zumax(needed,
			grow->alloc + grow->alloc / 4) + 16;
		void **ptr = ptr_ptr;
		void *hold = realloc(*ptr, grow->elem_size * new_len);
		if (!hold) {
			return false;
		}
		*ptr = hold;
		grow->alloc = new_len;
	}
	return true;
}

bool wugrow_recheck(void *restrict ptr_ptr, struct wugrow *grow) {
	return wugrow_reserve(ptr_ptr, grow, 0);
}

struct wugrow wugrow_init(const size_t elem_size) {
	return (struct wugrow) {
		.elem_size = elem_size,
	};
}


struct wuptr wuptr_mem(const void *str, const size_t len) {
	return (struct wuptr){.len = len, .ptr = str};
}

struct wuptr wuptr_str(const char *str) {
	return wuptr_mem(str, strlen(str));
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
	return !strncmp((char *)w1.ptr, s2, w1.len);
}


void wustr_free(struct wustr *w) {
	free(w->str);
}

bool wustr_realloc(struct wustr *w, const size_t len) {
	void *hold = realloc(w->str, len + 1);
	if (hold) {
		w->len = len;
		w->str = hold;
	}
	return (bool)hold;
}

bool wustr_malloc(struct wustr *w, const size_t len) {
	w->len = len;
	w->str = malloc(len + 1);
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
	return fwrite(w->str, 1, w->len, out);
}

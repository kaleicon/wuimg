#include <stdlib.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>

#include "wustr.h"
#include "common.h"

bool wustr_eq(const struct wustr *w1, const struct wustr *w2) {
	if (w1->len == w2->len) {
		return !memcmp(w1->str, w2->str, zumin(w1->len, w2->len));
	}
	return false;
}

struct wustr wustr_const(const struct wustr_mut *orig) {
	return (struct wustr){.len = orig->len, .str = orig->str};
}

struct wustr wustr_from_str(const char *str) {
	return (struct wustr){.len = strlen(str), .str = str};
}


struct wustr_mut * wustr_realloc(struct wustr_mut *w, const size_t len) {
	struct wustr_mut *hold = realloc(w, sizeof(*w) + len + 1);
	if (hold) {
		hold->len = len;
	}
	return hold;
}

struct wustr_mut * wustr_malloc(const size_t len) {
	return wustr_realloc(NULL, len);
}

struct wustr_mut * wustr_memdup(const char *str, size_t len) {
	struct wustr_mut *w = wustr_malloc(len);
	if (w) {
		memcpy(w->str, str, len);
		w->str[len] = 0;
	}
	return w;
}

struct wustr_mut * wustr_strdup(const char *str) {
	return wustr_memdup(str, strlen(str));
}

bool wustr_grow(struct wustr_mut **w, size_t new_len) {
	const size_t diff = sizeof(**w) + 1;

	size_t cur_len = (*w)->len + diff;
	new_len += diff;
	if (grow_buffer(w, &cur_len, new_len, 1)) {
		(*w)->len = cur_len - diff;
		return true;
	}
	return false;
}

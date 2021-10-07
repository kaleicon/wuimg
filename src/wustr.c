#include <stdlib.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>

#include "wustr.h"
#include "common.h"

struct wustr wustr_const(const struct wustr_mut *orig) {
	return (struct wustr){.len = orig->len, .str = orig->str};
}

struct wustr wustr_str(const char *str) {
	return (struct wustr){.len = strlen(str), .str = (unsigned char *)str};
}

bool wustr_suffix(const struct wustr w1, const struct wustr w2) {
	if (w1.len >= w2.len) {
		const size_t diff = w1.len - w2.len;
		return !memcmp(w1.str + diff, w2.str, w2.len);
	}
	return false;
}

bool wustr_suffix_str(const struct wustr w1, const char *s2) {
	return wustr_suffix(w1, wustr_str(s2));
}

bool wustr_eq(const struct wustr w1, const struct wustr w2) {
	if (w1.len == w2.len) {
		return !memcmp(w1.str, w2.str, w1.len);
	}
	return false;
}

bool wustr_eq_str(const struct wustr w1, const char *s2) {
	return wustr_eq(w1, wustr_str(s2));
}


void wustr_free(struct wustr_mut *w) {
	free(w->str);
}

bool wustr_malloc(struct wustr_mut *w, const size_t len) {
	w->len = len;
	w->str = malloc(len + 1);
	return (bool)w->str;
}

bool wustr_memdup(struct wustr_mut *w, const char *str, const size_t len) {
	if (wustr_malloc(w, len)) {
		memcpy(w->str, str, len);
		w->str[len] = 0;
	}
	return (bool)w->str;
}

bool wustr_strdup(struct wustr_mut *w, const char *str) {
	return wustr_memdup(w, str, strlen(str));
}

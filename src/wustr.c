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

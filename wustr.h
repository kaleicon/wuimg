#ifndef WU_STR
#define WU_STR

#include <stdbool.h>
#include <stddef.h>

struct wustr_mut {
	size_t len;
	char str[];
};

struct wustr {
	size_t len;
	const char *str;
};

bool wustr_eq(const struct wustr *w1, const struct wustr *w2);

struct wustr wustr_const(const struct wustr_mut *str);

struct wustr wustr_from_str(const char *str);


struct wustr_mut * wustr_realloc(struct wustr_mut *w, size_t len);

struct wustr_mut * wustr_malloc(size_t len);

struct wustr_mut * wustr_memdup(const char *str, size_t len);

struct wustr_mut * wustr_strdup(const char *str);

bool wustr_grow(struct wustr_mut **w, size_t new_len);

#endif /* WU_STR */

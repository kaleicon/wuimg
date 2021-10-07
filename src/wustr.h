#ifndef WU_STR
#define WU_STR

#include <stdbool.h>
#include <stddef.h>

struct wustr_mut {
	size_t len;
	unsigned char *str;
};

struct wustr {
	size_t len;
	const unsigned char *str;
};

struct wustr wustr_const(const struct wustr_mut *orig);

struct wustr wustr_str(const char *str);

bool wustr_suffix(const struct wustr w1, const struct wustr w2);

bool wustr_suffix_str(const struct wustr w1, const char *s2);

bool wustr_eq(const struct wustr w1, const struct wustr w2);

bool wustr_eq_str(const struct wustr w1, const char *s2);


void wustr_free(struct wustr_mut *w);

bool wustr_malloc(struct wustr_mut *w, size_t len);

bool wustr_memdup(struct wustr_mut *w, const char *str, size_t len);

bool wustr_strdup(struct wustr_mut *w, const char *str);

#endif /* WU_STR */

#ifndef WU_STR
#define WU_STR

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

struct wugrow {
	size_t elem_size;
	size_t alloc;
	size_t pos;
};

struct wustr {
	size_t len;
	unsigned char *str;
};

struct wuptr {
	size_t len;
	const unsigned char *ptr;
};

bool wugrow_reserve(void *restrict ptr_ptr, struct wugrow *grow, size_t extra);

bool wugrow_recheck(void *restrict ptr_ptr, struct wugrow *grow);

struct wugrow wugrow_init(size_t elem_size);


struct wuptr wuptr_mem(const void *str, size_t len);

struct wuptr wuptr_str(const char *str);

bool wuptr_suffix(struct wuptr w1, struct wuptr w2);

bool wuptr_suffix_str(struct wuptr w1, const char *s2);

bool wuptr_eq(struct wuptr w1, struct wuptr w2);

bool wuptr_eq_str(struct wuptr w1, const char *s2);


void wustr_free(struct wustr *w);

bool wustr_realloc(struct wustr *w, size_t len);

bool wustr_malloc(struct wustr *w, size_t len);

bool wustr_memdup(struct wustr *w, const char *str, size_t len);

bool wustr_append_line(struct wustr *w, const char *str, bool strip_trailing_spaces);

size_t wustr_print(const struct wustr *w, FILE *out);

#endif /* WU_STR */

#ifndef COMMON_MEMPARSER
#define COMMON_MEMPARSER

#include <inttypes.h>

#include "../wustr.h"

struct mp_parser { // ATM machine
	size_t pos;
	size_t len;
	const unsigned char *mem;
};

unsigned char mp_next_char_unsafe(struct mp_parser *mp);

size_t mp_skip_space_unsafe(struct mp_parser *mp);

size_t mp_get_uint_unsafe(struct mp_parser *mp, long *val);


bool mp_set_pos(struct mp_parser *mp, size_t pos);

void mp_skip_blank(struct mp_parser *mp);

size_t mp_skip_space(struct mp_parser *mp);

void mp_skip_nonspace(struct mp_parser *mp);

void mp_skip_line(struct mp_parser *mp);

int mp_next_char(struct mp_parser *mp);

int mp_next_nonblank(struct mp_parser *mp);

int mp_next_nonspace(struct mp_parser *mp);

struct wuptr mp_next_remaining(struct mp_parser *mp, size_t len);

const uint8_t * mp_next_slice(struct mp_parser *mp, size_t len);

struct wuptr mp_get_word(struct mp_parser *mp);

size_t mp_get_int(struct mp_parser *mp, size_t digits, long *val);

size_t mp_get_uint(struct mp_parser *mp, size_t digits, long *val);

size_t mp_get_xint(struct mp_parser *mp, size_t digits, long *val);

struct wuptr mp_remaining_at(const struct mp_parser *mp, size_t pos,
size_t len);

const uint8_t * mp_slice_at(const struct mp_parser *mp, size_t pos,
size_t len);

struct mp_parser mp_parser_mem(size_t len, const void *restrict mem);

#endif /* COMMON_MEMPARSER */

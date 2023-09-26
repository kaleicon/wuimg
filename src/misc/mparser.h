// SPDX-License-Identifier: 0BSD
#ifndef COMMON_MEMPARSER
#define COMMON_MEMPARSER

#include <inttypes.h>

#include "misc/file.h"
#include "misc/wustr.h"

struct mparser {
	size_t pos;
	size_t len;
	const unsigned char *mem;
};

unsigned char mp_next_char_unsafe(struct mparser *mp);

size_t mp_skip_space_unsafe(struct mparser *mp);

size_t mp_scan_uint_unsafe(struct mparser *mp, long *val);


void mp_skip_blank(struct mparser *mp);

size_t mp_skip_space(struct mparser *mp);

void mp_skip_line(struct mparser *mp);

int mp_next_char(struct mparser *mp);

int mp_next_nonblank(struct mparser *mp);

int mp_next_nonspace(struct mparser *mp);

struct wuptr mp_next_word(struct mparser *mp);

struct wuptr mp_next_remaining(struct mparser *mp, size_t len);

const uint8_t * mp_next_slice(struct mparser *mp, size_t len);

size_t mp_scan_uint(struct mparser *mp, size_t digits, long *val);

size_t mp_scan_int(struct mparser *mp, size_t digits, long *val);

size_t mp_scan_xint(struct mparser *mp, size_t digits, long *val);

struct wuptr mp_remaining_at(const struct mparser *mp, size_t pos,
size_t len);

const uint8_t * mp_slice_at(const struct mparser *mp, size_t pos,
size_t len);

struct mparser mp_mem(size_t len, const void *restrict mem);

struct mparser mp_map(struct map_info mm);

#endif /* COMMON_MEMPARSER */

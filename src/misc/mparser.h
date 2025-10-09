// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2023 kaleido
#ifndef COMMON_MEMPARSER
#define COMMON_MEMPARSER

#include <stddef.h>
#include <stdint.h>

#include "misc/wustr.h"

struct mparser {
	size_t pos;
	size_t len;
	const unsigned char *mem;
};

unsigned char mp_next_char_unsafe(struct mparser *mp);

size_t mp_skip_space_unsafe(struct mparser *mp);

size_t mp_scan_uint_unsafe(struct mparser *mp, uintmax_t *val);


size_t mp_skip_blank(struct mparser *mp);

size_t mp_skip_space(struct mparser *mp);

bool mp_skip_until(struct mparser *mp, int c);

int mp_cur_char(struct mparser *mp);

int mp_next_char(struct mparser *mp);

int mp_next_nonblank(struct mparser *mp);

int mp_next_nonspace(struct mparser *mp);

struct wuptr mp_next_word(struct mparser *mp);


const uint8_t * mp_slice_at(const struct mparser *mp, size_t pos,
size_t len);

struct wuptr mp_avail_at(const struct mparser *mp, size_t pos,
size_t len);

const uint8_t * mp_slice(struct mparser *mp, size_t len);

struct wuptr mp_avail(struct mparser *mp, size_t len);

struct wuptr mp_remaining(struct mparser *mp);

bool mp_upto(struct mparser *mp, struct wuptr *out, char chr);


size_t mp_scan_uint(struct mparser *mp, size_t digits, uintmax_t *val);

size_t mp_scan_int(struct mparser *mp, size_t digits, intmax_t *val);

size_t mp_scan_xint(struct mparser *mp, size_t digits, uintmax_t *val);

size_t mp_scan_anyuint(struct mparser *mp, size_t digits, uintmax_t *val);


size_t mp_seek_cur(struct mparser *mp, ptrdiff_t pos);

size_t mp_seek_set(struct mparser *mp, size_t pos);


struct mparser mp_mem(size_t len, const void *restrict mem);

struct mparser mp_wuptr(struct wuptr mm);

#endif /* COMMON_MEMPARSER */

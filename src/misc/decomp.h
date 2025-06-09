// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2023 kaleido
#ifndef MISC_DECOMP
#define MISC_DECOMP

#include <stddef.h>
#include <stdint.h>

size_t decomp_topbyterle(uint8_t *restrict dst, size_t dst_elems,
const uint8_t *restrict src, size_t src_len, size_t size);

size_t decomp_topbitrle(uint8_t *restrict dst, size_t dst_len,
const uint8_t *restrict src, size_t src_len, size_t pixel_size);

size_t decomp_packbits(unsigned char *restrict dst, size_t dst_len,
const signed char *restrict src, size_t src_len);

#endif /* MISC_DECOMP */

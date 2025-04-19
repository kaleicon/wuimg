// SPDX-License-Identifier: 0BSD
#ifndef MISC_DECOMP
#define MISC_DECOMP

#include <stdint.h>

size_t decomp_topbitrle(uint8_t *restrict dst, const size_t dst_len,
const uint8_t *restrict src, const size_t src_len, const size_t pixel_size);

size_t decomp_packbits(unsigned char *restrict dst, const size_t dst_len,
const signed char *restrict src, const size_t src_len);

#endif /* MISC_DECOMP */

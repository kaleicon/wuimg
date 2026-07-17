// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2026 kaleido

/* If we're not being compiled with rust, then deflate using zlib, which
 * depending on the system could be zlib-ng or, more likely, old zlib. */

#include <stdint.h>

#include <zlib.h>

size_t decomp_deflate(uint8_t *restrict dst, const size_t dst_len,
const uint8_t *restrict src, const size_t src_len) {
	uLong out = (uLong)dst_len;
	uncompress(dst, &out, src, src_len);
	return (size_t)out;
}

// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2023 kaleido
#include <string.h>

#include "misc/decomp.h"
#include "misc/math.h"
#include "misc/mem.h"

size_t decomp_topbyterle(uint8_t *restrict dst, const size_t dst_elems,
const uint8_t *restrict src, const size_t src_len, const size_t size) {
	const size_t packet_size = size + 1;
	const size_t src_elems = zumin(src_len/packet_size, dst_elems);
	size_t s = 0;
	size_t d = 0;
	while (s < src_elems) {
		const uint8_t cnt = src[s*packet_size];
		if (dst_elems - d < cnt) {
			break;
		}
		memwordset(dst + d*size, src + s*packet_size + 1, size, cnt);
		d += cnt;
		++s;
	}
	return d;
}

size_t decomp_topbitrle(uint8_t *restrict dst, const size_t dst_len,
const uint8_t *restrict src, const size_t src_len, const size_t pixel_size) {
	size_t s = 0;
	size_t d = 0;
	while (src_len - s >= pixel_size + 1) {
		const uint8_t packet = src[s];
		const size_t len = (packet & 0x7f) + 1U;
		const size_t bytes = len*pixel_size;
		if (dst_len - d < bytes) {
			break;
		}

		++s;
		if (packet & 0x80) {
			memwordset(dst + d, src + s, pixel_size, len);
			s += pixel_size;
		} else {
			if (src_len - s < bytes) {
				break;
			}
			memcpy(dst + d, src + s, bytes);
			s += bytes;
		}
		d += bytes;
	}
	return d;
}

size_t decomp_packbits(unsigned char *restrict dst, const size_t dst_len,
const signed char *restrict src, const size_t src_len) {
	size_t d = 0;
	size_t s = 0;
	while (src_len - s >= 2) {
		const signed char run = src[s];
		++s;
		size_t cnt;
		if (run < 0) {
			cnt = (size_t)(1 - run);
			if (dst_len - d < cnt) {
				break;
			}
			memset(dst + d, ((const unsigned char *)src)[s], cnt);
			++s;
		} else {
			cnt = (size_t)(1 + run);
			if (dst_len - d < cnt || src_len - s < cnt) {
				break;
			}
			memcpy(dst + d, src + s, cnt);
			s += cnt;
		}
		d += cnt;
	}
	return d;
}

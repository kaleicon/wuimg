// SPDX-License-Identifier: 0BSD
#include <string.h>

size_t decomp_pack_bits(unsigned char *restrict dst, const size_t dst_len,
const signed char *restrict src, const size_t src_len) {
	size_t d = 0;
	size_t s = 0;
	while (s + 1 < src_len && d < dst_len) {
		const signed char run = src[s];
		++s;
		size_t cnt;
		if (run < 0) {
			cnt = (size_t)(1 - run);
			if (d + cnt > dst_len) {
				break;
			}
			memset(dst + d, ((const unsigned char *)src)[s], cnt);
			++s;
		} else {
			cnt = (size_t)(1 + run);
			if (d + cnt > dst_len || s + cnt > src_len) {
				break;
			}
			memcpy(dst + d, src + s, cnt);
			s += cnt;
		}
		d += cnt;
	}
	return d;
}

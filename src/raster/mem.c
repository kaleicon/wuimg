#include <string.h>
#include <stdint.h>

uint8_t memcycle(void *dst, const size_t len) {
	uint8_t *d = dst;
	const uint8_t val = d[len];
	memmove(d + 1, d, len);
	d[0] = val;
	return val;
}

void memtessel(void *restrict dst, const void *restrict src, const size_t size,
size_t bytes) {
	uint8_t *d = dst;
	const uint8_t *s = src;
	while (bytes > size) {
		memcpy(d, s, size);
		d += size;
		bytes -= size;
	}
	memcpy(d, s, bytes);
}

void memrepeat(void *dst, size_t pos, size_t offset, size_t count) {
	uint8_t *d = (uint8_t *)dst + pos;
	const uint8_t *s = d - offset;
	memtessel(d, s, offset, count);
}

void memrepeat_or_zero(void *dst, size_t pos, size_t offset, size_t count) {
	uint8_t *d = dst;
	if (offset > pos + count) {
		memset(d + pos, 0, count);
	} else {
		if (offset > pos) {
			memset(d + pos, 0, offset - pos);
			pos += offset - pos;
			count -= offset - pos;
		}
		memrepeat(dst, pos, offset, count);
	}
}

#include <string.h>
#include <stdint.h>

void memrepeat(void *dst, size_t pos, size_t offset, size_t count) {
	uint8_t *d = (uint8_t *)dst + pos;
	const uint8_t *src = d - offset;
	while (count > offset) {
		memcpy(d, src, offset);
		d += offset;
		count -= offset;
		offset *= 2;
	}
	memcpy(d, src, count);
}

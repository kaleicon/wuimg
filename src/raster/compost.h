#ifndef ANIM_COMMON
#define ANIM_COMMON

#include <stdint.h>
#include <stdbool.h>

struct frame_info {
	size_t x, y;
	size_t w, h;
	int msec;
	bool keyframe;
};

void compost_alpha_blend(void *restrict dst, size_t w,
const void *restrict src, const struct frame_info *fr);

void compost_overwrite(void *restrict dst, size_t w, uint8_t ch,
const void *restrict src, const struct frame_info *fr);

void compost_clear(void *restrict dst, size_t w, uint8_t ch, int c,
const struct frame_info *fr);

bool compost_bounds_check(size_t w, size_t h, const struct frame_info *fr);

#endif /* ANIM_COMMON */

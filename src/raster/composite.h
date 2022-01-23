#ifndef ANIM_COMMON
#define ANIM_COMMON

#include "../wudefs.h"

struct frame {
	size_t x, y;
	size_t w, h;
	int msec;
};

void compost_alpha_blend(void *restrict dst, size_t w, uint8_t ch,
const void *restrict src, const struct frame *fr);

void compost_overwrite(void *restrict dst, const size_t w, const uint8_t ch,
const void *restrict src, const struct frame *fr);

void composite_frame_alpha_blend(struct raw_img *img,
const unsigned char *restrict src, const struct frame_info *frame);

void composite_frame_overwrite(struct raw_img *img,
const unsigned char *restrict src, const struct frame_info *frame);

void composite_clear(struct raw_img *img, const struct frame_info *frame,
int c);

#endif /* ANIM_COMMON */

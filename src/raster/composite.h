#ifndef ANIM_COMMON
#define ANIM_COMMON

#include "../wudefs.h"

struct anim_frame {
	size_t x, y;
	size_t w, h;
};

void composite_frame_alpha_blend(struct raw_img *img,
const unsigned char *restrict src, const struct anim_frame *frame);

void composite_frame_overwrite(struct raw_img *img,
const unsigned char *restrict src, const struct anim_frame *frame);

void color_set(void *restrict dst, const void *restrict src,
size_t size, size_t nmemb);

/*
void composite_color(struct raw_img *img, const unsigned char *restrict color,
const struct anim_frame *frame);
*/

void composite_clear(struct raw_img *img, const struct anim_frame *frame,
int c);

void copy_unaffected(struct raw_img *img, const unsigned char *restrict prev,
const struct anim_frame *frame);

#endif /* ANIM_COMMON */

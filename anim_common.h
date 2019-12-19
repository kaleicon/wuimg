#ifndef ANIM_COMMON
#define ANIM_COMMON

#include "wudefs.h"

struct anim_frame {
	unsigned int top, left, width, height;
};

void copy_unaffected(struct raw_img *img, const unsigned char *prev,
const struct anim_frame *frame);

void color_set(unsigned char *restrict data,
const unsigned char *restrict color, const size_t data_len, const size_t ch);

#endif /* ANIM_COMMON */

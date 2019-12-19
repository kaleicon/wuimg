#include <string.h>

#include "wudefs.h"
#include "anim_common.h"

/*void composite_color(struct raw_img *img, const unsigned char *color,
const struct anim_frame *frame) {
	const size_t top = (size_t)frame->top;
	const size_t left = (size_t)frame->left;
	const size_t width = (size_t)frame->width;

	size_t start = (top * img->w + left) * img->channels;

}*/

void copy_unaffected(struct raw_img *img, const unsigned char *prev,
const struct anim_frame *frame) {
	const size_t top = (size_t)frame->top;
	const size_t left = (size_t)frame->left;
	const size_t width = (size_t)frame->width;
	const size_t height = (size_t)frame->height;

	size_t start = (top * img->w + left) * img->channels;
	const size_t copy_stride = (img->w - width) * img->channels;
	const size_t tail = (img->w * img->h - (height - 1) * img->w - width)
		* img->channels - start;

	memcpy(img->data, prev, start);
	if (copy_stride == 0) {
		start += width * height * img->channels;
	} else {
		start += width * img->channels;
		for (size_t i = 0; i < frame->height - 1; ++i) {
			memcpy(img->data + start, prev + start, copy_stride);
			start += img->w * img->channels;
		}
	}
	memcpy(img->data + start, prev + start, tail);
}

void color_set(unsigned char *restrict data,
const unsigned char *restrict color, const size_t data_len, const size_t ch) {
	_Bool the_truth = 1;
	for (size_t i = 1; i < ch; ++i) {
		the_truth = the_truth && color[0] == color[i];
	}
	if (the_truth) {
		memset(data, color[0], data_len * ch);
	} else {
		for (size_t i = 0; i < data_len; ++i) {
			data[i * ch] = color[0];
			data[i * ch + 1] = color[1];
			if (ch == 3) {
				data[i * ch + 2] = color[2];
			} else if (ch == 4) {
				data[i * ch + 2] = color[2];
				data[i * ch + 3] = color[3];
			}
		}
	}
}

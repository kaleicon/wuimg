#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <webp/decode.h>
#include <webp/demux.h>

#include "wudefs.h"
#include "common.h"
#include "anim_common.h"

/* Many functions here are copies from the ones in dec_gif.c, only taking in a
 * different struct. */

static enum wu_error_type map_status(VP8StatusCode status, const char **msg) {
	switch (status) {
//	case VP8_STATUS_OK:
//		return "All OK";
	case VP8_STATUS_OUT_OF_MEMORY:
		*msg = "Out of memory error";
		return wu_alloc_error;
	case VP8_STATUS_INVALID_PARAM:
		*msg = "Invalid parameters";
		return wu_decoding_error;
	case VP8_STATUS_BITSTREAM_ERROR:
		*msg = "Bitstream error";
		return wu_decoding_error;
	case VP8_STATUS_UNSUPPORTED_FEATURE:
		*msg = "Unsupported feature";
		return wu_decoding_error;
	case VP8_STATUS_SUSPENDED:
		*msg = "Suspended";
		return wu_unknown_error;
	case VP8_STATUS_USER_ABORT:
		*msg = "User abort";
		return wu_unknown_error;
	case VP8_STATUS_NOT_ENOUGH_DATA:
		*msg = "Not enough data";
		return wu_unexpected_eof;
	default:
		*msg = "No message defined for this error code";
		return wu_unknown_error;
	}
}

static void blend_rgba_on_rgba_pixel(unsigned char *restrict d,
const unsigned char *restrict s) {
	const int ch = 4;
	switch (s[3]) {
	case 0xff: // Only case where (1 - src.A / 255) == 0
		memcpy(d, s, ch);
		return;
	case 0x00: // With the above, only case where blend.A == 0
		return;
	}

	const long int alpha = s[3] + d[3];
	for (int k = 0; k < ch - 1; ++k) {
		d[k] = (unsigned char)(
			(s[k] * s[3] + d[k] * d[3]) / alpha);
	}
	d[3] = (unsigned char)(s[3] + d[3]);
}

static void blend_rgba_on_rgb_pixel(unsigned char *restrict d,
const unsigned char *restrict s) {
	const int ch = 3;
	switch (s[3]) {
	case 0x00:
		return;
	case 0xff: // Only case where (1 - src_alpha / 255) == 0
		memcpy(d, s, ch);
		return;
	}

	const int dst_alpha = 0xff;
	const long int alpha = s[3] + dst_alpha;
	for (int k = 0; k < ch; ++k) {
		d[k] = (unsigned char)(
			(s[k] * s[3] + d[k] * s[3]) / alpha);
	}
}


static void blend_row(unsigned char *restrict d, const unsigned char *restrict s,
const size_t len, const size_t ch) {
	/* The WebP spec gives the following formula for alpha blending.

	//	blend.A = src.A + dst.A * (1 - src.A / 255)
	//	if blend.A = 0 then
	//		blend.RGB = 0
	//	else
	//		blend.RGB = (src.RGB * src.A +
	//			dst.RGB * dst.A * (1 - src.A / 255)) / blend.A

	 * https://web.archive.org/web/https://developers.google.com/speed/webp/docs/riff_container
	*/

	const size_t sch = 4;

	if (ch == 3) {
		for (size_t j = 0; j < len; ++j) {
			blend_rgba_on_rgb_pixel(d + j*ch, s + j*sch);
		}
	} else if (ch == 4) {
		for (size_t j = 0; j < len; ++j) {
			blend_rgba_on_rgba_pixel(d + j*ch, s + j*sch);
		}
	}
}

static void composite_frame_alpha_blend(struct raw_img *img,
const unsigned char *src, const WebPIterator *iter) {
	size_t dst_pos = ((size_t)iter->y_offset * img->w
		+ (size_t)iter->x_offset) * img->channels;

	if ((size_t)iter->width == img->w) {
		blend_row(img->data + dst_pos, src,
			(size_t)(iter->width * iter->height), img->channels);
	} else {
		size_t src_pos = 0;
		for (int i = 0; i < iter->height; ++i) {
			blend_row(img->data + dst_pos, src + src_pos,
				(size_t)iter->width, img->channels);
			dst_pos += img->w * img->channels;
			src_pos += (size_t)iter->width * 4;
		}
	}
}

static void composite_frame_no_blend(struct raw_img *img,
const unsigned char *restrict src, const WebPIterator *iter) {
	const size_t src_width = (size_t)iter->width * img->channels;
	size_t dst_pos = ((size_t)iter->y_offset * img->w
		+ (size_t)iter->x_offset) * img->channels;

	if ((size_t)iter->width == img->w) {
		memcpy(img->data + dst_pos, src,
			src_width * (size_t)iter->height);
	} else {
		size_t src_pos = 0;
		for (int i = 0; i < iter->height; ++i) {
			memcpy(img->data + dst_pos, src + src_pos, src_width);
			dst_pos += img->w * img->channels;
			src_pos += src_width;
		}
	}
}


static void composite_frame(struct raw_img *img, const unsigned char *dec_buf,
const WebPIterator *iter) {
	if (img->channels == 4) {
		if (iter->blend_method == WEBP_MUX_BLEND) {
			composite_frame_alpha_blend(img, dec_buf, iter);
		} else {
			composite_frame_no_blend(img, dec_buf, iter);
		}
	} else {
		if (!iter->has_alpha || iter->blend_method == WEBP_MUX_NO_BLEND) {
			composite_frame_no_blend(img, dec_buf, iter);
		} else {
			composite_frame_alpha_blend(img, dec_buf, iter);
		}
	}

}

static void composite_color(struct raw_img *img, const unsigned char *color,
const struct anim_frame *frame) {
	size_t dst_pos = (frame->top * img->w + frame->left) * img->channels;

	if (frame->width == img->w) {
		color_set(img->data + dst_pos, color, img->w * img->h,
			img->channels);
	} else {
		for (size_t i = 0; i < frame->height; ++i) {
			color_set(img->data + dst_pos, color, img->w,
				img->channels);
			dst_pos += img->w * img->channels;
		}
	}
}

static void iter_to_anim_frame(struct anim_frame *frame,
const WebPIterator *iter) {
	frame->width = (unsigned int)iter->width;
	frame->height = (unsigned int)iter->height;
	frame->left = (unsigned int)iter->x_offset;
	frame->top = (unsigned int)iter->y_offset;
}

static int is_covered(const WebPIterator *iter, const struct anim_frame *prev) {
	const int w_diff = iter->width - (int)prev->width;
	const int h_diff = iter->height - (int)prev->height;
	const int x_diff = iter->x_offset - (int)prev->left;
	const int y_diff = iter->y_offset - (int)prev->top;
	return (w_diff - x_diff >= 0) && (h_diff - y_diff >= 0);
}

static unsigned char compute_properties(const WebPDemuxer *dmux,
WebPIterator *iter, unsigned char *restrict bg) {
	// Color is in BGRA order
	const uint32_t color = WebPDemuxGetI(dmux, WEBP_FF_BACKGROUND_COLOR);
	bg[3] = (unsigned char)(color & 0x000000ff);
	bg[0] = (unsigned char)((color & 0x0000ff00) >> 8);
	bg[1] = (unsigned char)((color & 0x00ff0000) >> 16);
	bg[2] = (unsigned char)((color & 0xff000000) >> 24);

	unsigned char channels = 3;
	struct anim_frame prev_frame = {0, 0, 0, 0};
	WebPMuxAnimDispose prev_disp = WEBP_MUX_DISPOSE_NONE;
	int i = 0;
	do {
		/* Reasonably exhaustive tests for alpha. */
		if (iter->has_alpha) {
			if (iter->blend_method == WEBP_MUX_NO_BLEND) {
				channels = 4;
			}
		}
		if (bg[3] != 0xff) {
			if (prev_disp == WEBP_MUX_DISPOSE_BACKGROUND) {
				const int it_is = is_covered(iter, &prev_frame);
				if (iter->has_alpha || !it_is) {
					channels = 4;
					break;
				}
			}

			prev_disp = iter->dispose_method;
			if (prev_disp == WEBP_MUX_DISPOSE_BACKGROUND) {
				iter_to_anim_frame(&prev_frame, iter);
			}
		}

		++i;
	} while (channels == 3 && WebPDemuxNextFrame(iter));

	if (channels == 4) {
		memset(bg, 0, 4);
	}

	WebPDemuxGetFrame(dmux, 1, iter);
	return channels;
}

static void setup_common_decoding_options(WebPDecoderConfig *config) {
	config->options.bypass_filtering = 1;
	config->options.no_fancy_upsampling = 1;
	config->options.use_threads = 1;
	config->output.is_external_memory = 1;
}

__attribute__((unused))static VP8StatusCode demuxer_decode(
struct image_file *infile, const WebPData *data, WebPDecoderConfig *config) {
	WebPDemuxer *dmux = WebPDemux(data);

	WebPIterator iter;
	if (!WebPDemuxGetFrame(dmux, 1, &iter)) {
		WebPDemuxDelete(dmux);
		return VP8_STATUS_BITSTREAM_ERROR;
	}

	const unsigned int canvas_width = (unsigned int)config->input.width;
	const unsigned int canvas_height = (unsigned int)config->input.height;
	const size_t canvas_size = canvas_width * canvas_height;
	// input.has_alpha is true if any frame contains any alpha
	unsigned char *dec_buffer = malloc(canvas_size *
		(size_t)(3 + !!(config->input.has_alpha)) );
	if (!dec_buffer) {
		WebPDemuxDelete(dmux);
		return VP8_STATUS_OUT_OF_MEMORY;
	}

	unsigned char background[] = {0, 0, 0, 0};
	const unsigned char channels = compute_properties(dmux, &iter,
		background);

	config->output.u.RGBA.rgba = dec_buffer;

	const size_t frame_count = WebPDemuxGetI(dmux, WEBP_FF_FRAME_COUNT);
	struct raw_img *img = alloc_sub_images(infile, frame_count);

	struct anim_frame prev_desc;
	WebPMuxAnimDispose dispose = WEBP_MUX_DISPOSE_NONE; // Temp value.
	VP8StatusCode status = VP8_STATUS_OK;
	size_t i = 0;
	do {
		// Decoding is done to dec_buffer, then composited as required.
		int dec_channels;
		if (channels == 4 || iter.has_alpha) {
			dec_channels = 4;
			config->output.colorspace = MODE_RGBA;
		} else {
			dec_channels = 3;
			config->output.colorspace = MODE_RGB;
		}
		const int stride = iter.width * dec_channels;
		const size_t buf_size = (size_t)(stride * iter.height);
		config->output.u.RGBA.stride = stride;
		config->output.u.RGBA.size = buf_size;

		status = WebPDecode(iter.fragment.bytes, iter.fragment.size,
			config);
		if (status != VP8_STATUS_OK) {
			printf("Failed to decode frame %zu from %s\n",
				i, infile->name);
			break;
		}

		img[i].data = malloc(canvas_size * channels);
		img[i].w = canvas_width;
		img[i].h = canvas_height;
		img[i].channels = channels;
		img[i].bitdepth = 8;
		img[i].msec = iter.duration;

		if (i == 0) {
			if (channels == 3) {
				color_set(img[i].data, background, canvas_size,
					channels);
			} else {
				memset(img[i].data, 0, canvas_size * channels);
			}
			composite_frame(&img[i], dec_buffer, &iter);
		} else {
			switch (dispose) {
			case WEBP_MUX_DISPOSE_BACKGROUND:
				copy_unaffected(&img[i], img[i-1].data,
					&prev_desc);
				composite_color(&img[i], background,
					&prev_desc);
				composite_frame(&img[i], dec_buffer, &iter);
				break;
			case WEBP_MUX_DISPOSE_NONE:
				memcpy(img[i].data, img[i-1].data,
					canvas_size * channels);
				composite_frame(&img[i], dec_buffer, &iter);
				break;
			}
		}

		dispose = iter.dispose_method;
		if (dispose == WEBP_MUX_DISPOSE_BACKGROUND) {
			iter_to_anim_frame(&prev_desc, &iter);
		}
		++i;
	} while (WebPDemuxNextFrame(&iter));

	if (i < infile->nr) {
		fit_sub_images(infile, i);
	}

	free(dec_buffer);
	WebPDemuxReleaseIterator(&iter);
	WebPDemuxDelete(dmux);
	return status;
}

__attribute__((unused))static VP8StatusCode animation_library_decode(
struct image_file *infile, const WebPData *data) {
	WebPAnimDecoderOptions options;
	WebPAnimDecoderOptionsInit(&options);
	options.color_mode = MODE_RGBA;
	options.use_threads = 1;

	WebPAnimDecoder *dec = WebPAnimDecoderNew(data, &options);
	WebPAnimInfo info;
	WebPAnimDecoderGetInfo(dec, &info);

	struct raw_img *img = alloc_sub_images(infile, info.frame_count);
	const size_t canvas_size = info.canvas_width * info.canvas_height * 4;
	size_t i = 0;
	while (WebPAnimDecoderHasMoreFrames(dec)) {
		unsigned char *buf;
		int msec;
		WebPAnimDecoderGetNext(dec, &buf, &msec);

		img[i].w = info.canvas_width;
		img[i].h = info.canvas_height;
		img[i].channels = 4;
		img[i].bitdepth = 8;
		img[i].msec = msec;

		img[i].data = malloc(canvas_size);
		memcpy(img[i].data, buf, canvas_size);
		++i;
	}

	WebPAnimDecoderDelete(dec);
	return VP8_STATUS_OK;
}

static VP8StatusCode single_image_decode(struct image_file *infile,
const WebPData *data, WebPDecoderConfig *config) {
	struct raw_img *img = alloc_sub_images(infile, 1);
	img->w = (unsigned int)config->input.width;
	img->h = (unsigned int)config->input.height;
	img->bitdepth = 8;
	if (config->input.has_alpha) {
		img->channels = 4;
		config->output.colorspace = MODE_RGBA;
	} else {
		img->channels = 3;
		config->output.colorspace = MODE_RGB;
	}

	const int stride = (int)(img->w * img->channels);
	const size_t buf_size = (size_t)stride * img->h;
	img->data = malloc(buf_size);
	config->output.u.RGBA.rgba = img->data;
	config->output.u.RGBA.stride = stride;
	config->output.u.RGBA.size = buf_size;

	return WebPDecode(data->bytes, data->size, config);
}

enum wu_error_type webp_dec(struct image_file *infile) {
	WebPData data;
	data.bytes = read_file_to_mem(infile->name, &data.size);
	if (!data.bytes) {
		return wu_open_error;
	}

	WebPDecoderConfig config;
	WebPInitDecoderConfig(&config);
	VP8StatusCode status = WebPGetFeatures(data.bytes, data.size,
		&config.input);
	if (status != VP8_STATUS_OK) {
		free((unsigned char *)data.bytes);
		WebPFreeDecBuffer(&config.output);
		return wu_invalid_header;
	}

	setup_common_decoding_options(&config);

	if (config.input.has_animation) {
		infile->is_animation = true;
//		status = animation_library_decode(infile, &data);
		status = demuxer_decode(infile, &data, &config);
	} else {
		infile->is_animation = false;
		status = single_image_decode(infile, &data, &config);
	}
	WebPFreeDecBuffer(&config.output);

	enum wu_error_type err = wu_ok;
	if (status != VP8_STATUS_OK) {
		const char *msg;
		err = map_status(status, &msg);
		infile->err_msg = strdup(msg);
	}

	free((unsigned char *)data.bytes);
	return err;
}

// SPDX-License-Identifier: 0BSD
#include <jxl/decode.h>
#include <jxl/resizable_parallel_runner.h>

#include "wudefs.h"
#include "misc/math.h"

struct jpegxl_state {
	JxlDecoder *jd;
	void *runner;
	int idx;
	JxlBasicInfo info;
	JxlPixelFormat fmt;
};

static void cleanup_jpegxl(struct image_file *infile) {
	struct jpegxl_state *ds = infile->dec_state;
	if (ds->runner) {
		JxlResizableParallelRunnerDestroy(ds->runner);
	}
	JxlDecoderDestroy(ds->jd);
}

static void set_colorspace(struct wuimg *img, JxlDecoder *jd) {
	const JxlColorProfileTarget target = JXL_COLOR_PROFILE_TARGET_DATA;
	JxlColorEncoding enc;
	if (JxlDecoderGetColorAsEncodedProfile(jd, NULL, target, &enc)
	== JXL_DEC_SUCCESS) {
		switch (enc.color_space) {
		case JXL_COLOR_SPACE_RGB:
			color_space_set_primaries(&img->cs,
				enc.white_point_xy[0],
				enc.white_point_xy[1],
				enc.primaries_red_xy[0],
				enc.primaries_red_xy[1],
				enc.primaries_green_xy[0],
				enc.primaries_green_xy[1],
				enc.primaries_blue_xy[0],
				enc.primaries_blue_xy[1]);
			// fallthrough
		case JXL_COLOR_SPACE_GRAY:
			if (enc.transfer_function == JXL_TRANSFER_FUNCTION_GAMMA) {
				color_space_set_gamma(&img->cs, 1/enc.gamma);
			} else {
				img->cs.transfer =
					(enum cicp_transfer)enc.transfer_function;
			}
			return;
		case JXL_COLOR_SPACE_XYB:
		case JXL_COLOR_SPACE_UNKNOWN:
			break;
		}
	}

	size_t icc_size;
	if (JxlDecoderGetICCProfileSize(jd, NULL, target, &icc_size)
	== JXL_DEC_SUCCESS) {
		void *icc = malloc(icc_size);
		if (icc) {
			if (JxlDecoderGetColorAsICCProfile(jd, NULL, target,
			icc, icc_size) == JXL_DEC_SUCCESS) {
				color_space_set_icc_owned(&img->cs, icc,
					icc_size);
			} else {
				free(icc);
			}
		}
	}
}

static bool set_fmt(const struct wuimg *img, JxlPixelFormat *fmt) {
	fmt->num_channels = img->channels;
	fmt->align = 1 << img->align_sh;
	switch (img->bitdepth) {
	case 8: fmt->data_type = JXL_TYPE_UINT8; break;
	case 16:
		fmt->data_type = (img->attr == pix_float)
			? JXL_TYPE_FLOAT16 : JXL_TYPE_UINT16;
		break;
	case 32:
		fmt->data_type = JXL_TYPE_FLOAT;
		break;
	default:
		return false;
	}
	return true;
}

static enum wu_error render_frame(struct wuimg *img, struct jpegxl_state *ds) {
	for (;;) {
		switch (JxlDecoderProcessInput(ds->jd)) {
		case JXL_DEC_NEED_IMAGE_OUT_BUFFER:
			if (JxlDecoderSetImageOutBuffer(ds->jd, &ds->fmt,
			img->data, wuimg_size(img)) != JXL_DEC_SUCCESS) {
				return wu_invalid_params;
			}
			break;
		case JXL_DEC_FRAME:
			if (img->frames) {
				++ds->idx;
				JxlFrameHeader header;
				if (JxlDecoderGetFrameHeader(ds->jd, &header)
				!= JXL_DEC_SUCCESS) {
					return wu_invalid_params;
				}
				/* Notice that time units are given as ticks
				 * _per second_. Hence, you have to do 'den/num'
				 * to get the duration of a tick. */
				const uint32_t num = ds->info.animation.tps_numerator;
				const uint32_t den = ds->info.animation.tps_denominator;
				const int msec = (int)(
					(size_t)den*1000*header.duration / num);
				wuimg_frame_set(img, (size_t)ds->idx, 0, 0,
					img->w, img->h, msec, false);
			}
			break;
		case JXL_DEC_FULL_IMAGE:
			return wu_ok;
		default:
			return wu_decoding_error;
		}
	}
	return wu_decoding_error;
}

static void input_init(struct image_file *infile, struct jpegxl_state *ds) {
	JxlDecoderSetInput(ds->jd, infile->map.data, infile->map.len);
	JxlDecoderCloseInput(ds->jd);
	ds->idx = -1;
}

static void rewind_anim(struct image_file *infile, struct jpegxl_state *ds) {
	JxlDecoderRewind(ds->jd);
	/* Supposedly SubscribeEvents is kept between rewinds, but that's a lie.
	 * Took me a while to figure that one out. */
	JxlDecoderSubscribeEvents(ds->jd, JXL_DEC_FRAME | JXL_DEC_FULL_IMAGE);
	input_init(infile, ds);
}

static enum wu_error get_frame(struct image_file *infile, int idx) {
	struct jpegxl_state *ds = infile->dec_state;
	if (idx == ds->idx) {
		return wu_no_change;
	} else if (idx < ds->idx) {
		rewind_anim(infile, infile->dec_state);
	}
	const int diff = idx - ds->idx - 1;
	if (diff) {
		JxlDecoderSkipFrames(ds->jd, (size_t)diff);
		ds->idx += diff;
	}
	return render_frame(infile->sub_img, ds);
}

static enum wu_error jpegxl_callback(struct image_file *infile,
const struct wu_conf *wuconf, struct wu_state *state, const enum image_event ev) {
	(void)wuconf;
	if (ev) {
		return get_frame(infile, state->frame);
	}
	cleanup_jpegxl(infile);
	return wu_ok;
}

static enum wu_error gather_info(struct image_file *infile,
const struct wu_conf *wuconf, struct jpegxl_state *ds) {
	struct wuimg *img = infile->sub_img;
	for (;;) {
		switch (JxlDecoderProcessInput(ds->jd)) {
		case JXL_DEC_BASIC_INFO:
			if (JxlDecoderGetBasicInfo(ds->jd, &ds->info)
			!= JXL_DEC_SUCCESS) {
				return wu_invalid_header;
			}
			img->w = ds->info.xsize;
			img->h = ds->info.ysize;
			if (wuimg_exceeds_limit(img, wuconf)) {
				return wu_exceeds_size_limit;
			}
			img->channels = (uint8_t)(ds->info.num_color_channels
				+ (bool)ds->info.alpha_bits);
			img->bitdepth = (uint8_t)(1 <<
				ulog2(uclamp(ds->info.bits_per_sample, 8, 32) * 2 - 1));
			if (img->bitdepth > 8 && ds->info.exponent_bits_per_sample) {
				img->attr = pix_float;
			}
			img->alpha = ds->info.alpha_premultiplied
				? alpha_associated : alpha_unassociated;
			wuimg_exif_orientation(img, (int)ds->info.orientation);
			if (!set_fmt(img, &ds->fmt)) {
				return wu_unsupported_feature;
			}
			break;
		case JXL_DEC_COLOR_ENCODING:
			set_colorspace(img, ds->jd);
			break;
		case JXL_DEC_JPEG_RECONSTRUCTION:
			tree_bud_leaf_bool(&infile->metadata, "JPEG source", true);
			break;
		case JXL_DEC_FRAME:
			++ds->idx;
			JxlFrameHeader header;
			if (JxlDecoderGetFrameHeader(ds->jd, &header)
			!= JXL_DEC_SUCCESS) {
				return wu_invalid_params;
			} else if (header.is_last) {
				return wu_ok;
			}
			break;
		default:
			return wu_invalid_params;
		}
	}
	return wu_invalid_params;
}

static enum wu_error dec_wrap(struct image_file *infile,
const struct wu_conf *wuconf, struct jpegxl_state *ds) {
	struct wuimg *img = alloc_sub_images(infile, 1);
	if (!img) {
		return wu_alloc_error;
	}

	input_init(infile, ds);
	JxlDecoderSetKeepOrientation(ds->jd, JXL_TRUE);
	JxlDecoderSubscribeEvents(ds->jd, JXL_DEC_BASIC_INFO
		| JXL_DEC_COLOR_ENCODING
		| JXL_DEC_JPEG_RECONSTRUCTION
		| JXL_DEC_FRAME);

	enum wu_error st = gather_info(infile, wuconf, ds);
	if (st != wu_ok) {
		return st;
	}

	if (ds->info.have_animation) {
		if (!wuimg_frames_init(img, (size_t)(ds->idx + 1))) {
			return wu_alloc_error;
		}
		infile->events = ev_frame;
	}
	st = wuimg_alloc(img);
	if (st != wu_ok) {
		return st;
	}

	rewind_anim(infile, infile->dec_state);
	const uint32_t threads = JxlResizableParallelRunnerSuggestThreads(
		img->w, img->h);
	if (threads > 1) {
		ds->runner = JxlResizableParallelRunnerCreate(NULL);
		if (ds->runner) {
			JxlResizableParallelRunnerSetThreads(ds->runner, threads);
			JxlDecoderSetParallelRunner(ds->jd,
				JxlResizableParallelRunner, ds->runner);
		}
	}
	return get_frame(infile, 0);//render_frame(img, ds);
}

static enum wu_error jpegxl_dec(struct image_file *infile,
const struct wu_conf *wuconf) {
	enum wu_error err = wu_alloc_error;
	struct jpegxl_state *ds = calloc(sizeof(*ds), 1);
	if (ds) {
		ds->jd = JxlDecoderCreate(NULL);
		if (ds->jd) {
			infile->dec_state = ds;
			return dec_wrap(infile, wuconf, ds);
		}
		free(ds);
	}
	return err;
}

const struct image_fn jpegxl_fn = {.mmap = true,
	.dec = jpegxl_dec, .callback = jpegxl_callback};

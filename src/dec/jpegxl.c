// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2022 kaleido
#include <string.h>

#include <jxl/decode.h>
#include <jxl/resizable_parallel_runner.h>

#include "wudefs.h"
#include "misc/bit.h"
#include "misc/common.h"
#include "misc/endian.h"
#include "misc/math.h"
#include "misc/metadata.h"

struct jpegxl_state {
	JxlDecoder *jd;
	struct wustr box;
	size_t box_written;
	void *runner;
	int idx;
	bool decompress;
	enum metadata_type pending:8;
	JxlBasicInfo info;
	JxlPixelFormat fmt;
};

static void jpegxl_end(struct image_file *infile) {
	struct jpegxl_state *ds = infile->dec_state;
	if (ds->runner) {
		JxlResizableParallelRunnerDestroy(ds->runner);
	}
	JxlDecoderDestroy(ds->jd);
	wustr_free(&ds->box);
}

static bool require_box_buf(struct wustr *box, const uint64_t size) {
	return (box->len < size) ? wustr_realloc(box, size) : true;
}

static bool bigger_box_buf(struct wustr *box) {
	return wustr_realloc(box, box->len + (box->len/2));
}

static size_t release_box_buf(struct jpegxl_state *ds) {
	return ds->box.len - JxlDecoderReleaseBoxBuffer(ds->jd);
}

static void process_metadata(struct image_file *infile,
struct jpegxl_state *ds) {
	if (ds->pending) {
		size_t written = release_box_buf(ds);
		uint8_t *buf = ds->box.str;
		if (ds->pending == metadata_exif) {
			if (written > 4) {
				uint32_t off = buf_endian32(ds->box.str, big_endian);
				written -= 4;
				buf += 4;
				if (off < written) {
					buf += off;
					written -= off;
				}
			} else {
				written = 0;
			}
		}
		if (written) {
			metadata_parse(ds->pending, buf, written,
				&infile->metadata);
		}
		ds->pending = metadata_none;
	}
}

static void realloc_metadata(struct jpegxl_state *ds) {
	const size_t written = release_box_buf(ds);
	if (bigger_box_buf(&ds->box)) {
		JxlDecoderSetBoxBuffer(ds->jd, ds->box.str + written,
			ds->box.len - written);
	}
}

static void read_metadata(struct image_file *infile, struct jpegxl_state *ds) {
	ds->pending = metadata_none;
	ds->box_written = 0;
	JxlBoxType type;
	if (JxlDecoderGetBoxType(ds->jd, type, ds->decompress) != JXL_DEC_SUCCESS) {
		return;
	}

	enum metadata_type pending = metadata_none;
	if (!memcmp(type, "Exif", sizeof(type))) {
		pending = metadata_exif;
	} else if (!memcmp(type, "xml ", sizeof(type))) {
		pending = metadata_xmp;
	} else {
		if (!memcmp(type, "jbrd", sizeof(type))) {
			tree_bud_leaf_bool(&infile->metadata,
				"JPEG source", true);
		}
		return;
	}

	uint64_t size;
	if (JxlDecoderGetBoxSizeContents(ds->jd, &size) != JXL_DEC_SUCCESS
	&& !ds->box.len) {
		size = BUFSIZ;
	}
	if (!require_box_buf(&ds->box, size)) {
		return;
	}
	if (JxlDecoderSetBoxBuffer(ds->jd, ds->box.str, ds->box.len)
	== JXL_DEC_SUCCESS) {
		ds->pending = pending;
	}
}

static void get_primaries(struct color_space *cs, JxlColorEncoding *enc) {
	struct broken_cicp {
		JxlWhitePoint white;
		JxlPrimaries primaries;
		enum cicp_primaries cicp;
	} pairs[] = {
		{JXL_WHITE_POINT_D65, JXL_PRIMARIES_SRGB, cicp_primaries_bt709_6},
		{JXL_WHITE_POINT_D65, JXL_PRIMARIES_2100, cicp_primaries_bt2020_2},
		{JXL_WHITE_POINT_DCI, JXL_PRIMARIES_P3, cicp_primaries_smpte_rp_431_2},
	};
	for (size_t i = 0; i < ARRAY_LEN(pairs); ++i) {
		if (enc->primaries == pairs[i].primaries
		&& enc->white_point == pairs[i].white) {
			cs->primaries = pairs[i].cicp;
			return;
		}
	}
	color_space_set_primaries(cs,
		enc->white_point_xy[0],
		enc->white_point_xy[1],
		enc->primaries_red_xy[0],
		enc->primaries_red_xy[1],
		enc->primaries_green_xy[0],
		enc->primaries_green_xy[1],
		enc->primaries_blue_xy[0],
		enc->primaries_blue_xy[1]);
}

static void set_colorspace(struct wuimg *img, JxlDecoder *jd) {
	const JxlColorProfileTarget target = JXL_COLOR_PROFILE_TARGET_DATA;
	JxlColorEncoding enc;
	if (JxlDecoderGetColorAsEncodedProfile(jd, target, &enc)
	== JXL_DEC_SUCCESS) {
		switch (enc.color_space) {
		case JXL_COLOR_SPACE_RGB:
			get_primaries(&img->cs, &enc);
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
	if (JxlDecoderGetICCProfileSize(jd, target, &icc_size)
	== JXL_DEC_SUCCESS) {
		void *icc = malloc(icc_size);
		if (icc) {
			if (JxlDecoderGetColorAsICCProfile(jd, target,
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

static struct wu_st render_frame(struct wuimg *img, struct jpegxl_state *ds) {
	bool ok = true;
	do {
		switch (JxlDecoderProcessInput(ds->jd)) {
		case JXL_DEC_NEED_IMAGE_OUT_BUFFER:
			if (JxlDecoderSetImageOutBuffer(ds->jd, &ds->fmt,
			img->data, wuimg_size(img)) != JXL_DEC_SUCCESS) {
				return WUERR_HERE(wu_invalid_params);
			}
			break;
		case JXL_DEC_FRAME:
			if (img->frames) {
				++ds->idx;
				JxlFrameHeader header;
				if (JxlDecoderGetFrameHeader(ds->jd, &header)
				!= JXL_DEC_SUCCESS) {
					return WUERR_HERE(wu_invalid_params);
				}
				/* Time units are given as ticks per second.
				 * Hence, a frame is displayed for
				 * 'den * duration / num' seconds. */
				const uint32_t num = ds->info.animation.tps_numerator;
				const uint32_t den = ds->info.animation.tps_denominator;
				const uint32_t duration = den*header.duration;
				wuimg_frame_set(img, (size_t)ds->idx, 0, 0,
					img->w, img->h, duration, num, false);
			}
			break;
		case JXL_DEC_FULL_IMAGE:
		case JXL_DEC_SUCCESS:
			return WU_OK;
		default:
			ok = false;
		}
	} while (ok);
	return WUERR_HERE(wu_decoding_error);
}

static void input_init(struct image_file *infile, struct jpegxl_state *ds) {
	JxlDecoderSetInput(ds->jd, infile->map.ptr, infile->map.len);
	JxlDecoderCloseInput(ds->jd);
	ds->idx = -1;
}

static void rewind_anim(struct image_file *infile, struct jpegxl_state *ds) {
	JxlDecoderRewind(ds->jd);
	input_init(infile, ds);
}

static struct wu_st get_frame(struct image_file *infile, int idx) {
	struct jpegxl_state *ds = infile->dec_state;
	if (idx == ds->idx) {
		return WU_NO_CHANGE;
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

static struct wu_st event_jpegxl(struct image_file *infile,
const struct wu_conf *_c, struct wu_state *state, const enum image_event ev) {
	(void)_c;
	return (ev == ev_frame)
		? get_frame(infile, state->frame)
		: WU_NO_CHANGE;
}

static struct wu_st gather_info(struct image_file *infile,
struct jpegxl_state *ds) {
	struct wuimg *img = infile->sub_img;
	bool ok = true;
	do {
		switch (JxlDecoderProcessInput(ds->jd)) {
		case JXL_DEC_BASIC_INFO:
			if (JxlDecoderGetBasicInfo(ds->jd, &ds->info)
			!= JXL_DEC_SUCCESS) {
				return WUERR_HERE(wu_invalid_header);
			}
			img->w = ds->info.xsize;
			img->h = ds->info.ysize;
			img->channels = (uint8_t)(ds->info.num_color_channels
				+ (bool)ds->info.alpha_bits);
			img->bitdepth = bit_min_wordsize_bits(
				umin(ds->info.bits_per_sample, 32)
			);
			if (ds->info.exponent_bits_per_sample) {
				img->attr = pix_float;
			}
			img->alpha = ds->info.alpha_premultiplied
				? alpha_associated : alpha_unassociated;
			wuimg_exif_orientation(img, (int)ds->info.orientation);
			if (!set_fmt(img, &ds->fmt)) {
				return WUERR_HERE(wu_unsupported_feature);
			}
			break;
		case JXL_DEC_COLOR_ENCODING:
			set_colorspace(img, ds->jd);
			break;
		case JXL_DEC_FRAME:
			++ds->idx;
			JxlFrameHeader header;
			if (JxlDecoderGetFrameHeader(ds->jd, &header)
			!= JXL_DEC_SUCCESS) {
				return WUERR_HERE(wu_invalid_params);
			}
			break;
		case JXL_DEC_BOX:
			read_metadata(infile, ds);
			break;
		case JXL_DEC_BOX_NEED_MORE_OUTPUT:
			realloc_metadata(ds);
			break;
		case JXL_DEC_BOX_COMPLETE:
			process_metadata(infile, ds);
			break;
		case JXL_DEC_SUCCESS:
			return WU_OK;
		default:
			ok = false;
			break;
		}
	} while (ok);
	return WUERR_HERE(wu_invalid_header);
}

static struct wu_st init_jpegxl(struct image_file *infile,
const struct wu_conf *conf) {
	struct jpegxl_state *ds = infile->dec_state;
	ds->jd = JxlDecoderCreate(NULL);
	if (!ds->jd) {
		return WUERR_HERE(wu_alloc_error);
	}

	struct wuimg *img = infile->sub_img;
	input_init(infile, ds);
	JxlDecoderSetKeepOrientation(ds->jd, JXL_TRUE);
	JxlDecoderSubscribeEvents(ds->jd, JXL_DEC_BASIC_INFO
		| JXL_DEC_COLOR_ENCODING
		| JXL_DEC_BOX
		| JXL_DEC_FRAME
		| JXL_DEC_BOX_COMPLETE);
	ds->decompress = JxlDecoderSetDecompressBoxes(ds->jd, JXL_TRUE) == JXL_DEC_SUCCESS;

	struct wu_st st = gather_info(infile, ds);
	if (!wu_isok(st)) {
		return st;
	}

	if (ds->info.have_animation) {
		if (!wuimg_frames_init(img, (size_t)(ds->idx + 1))) {
			return WUERR_HERE(wu_alloc_error);
		}
	}
	const enum wu_error err = wuimg_alloc_limit(img, conf);
	if (err != wu_ok) {
		return WUERR_HERE(err);
	}

	rewind_anim(infile, infile->dec_state);
	JxlDecoderSubscribeEvents(ds->jd, JXL_DEC_FRAME | JXL_DEC_FULL_IMAGE);
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
	return get_frame(infile, 0);
}

const struct image_fn jpegxl_fn = {
	.mmap = true,
	.alloc_single = true,
	.state_size = sizeof(struct jpegxl_state),
	.init = init_jpegxl,
	.event = event_jpegxl,
	.end = jpegxl_end,
};

// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2025 kaleido
#include <inttypes.h>

#include <jxl/encode.h>

#include "misc/bit.h"
#include "misc/endian.h"
#include "misc/math.h"

#include "enc.h"

static const uint16_t APPROX_TIME_RES = 10000;
static const int NO_TRANSFER = -1;

struct jxl_state {
	JxlEncoder *enc;
	JxlEncoderFrameSettings *settings;
	JxlPixelFormat fmt;
	JxlBitDepth depth;
	bool round_time;
	const struct image_frames *frames;
	uint8_t buf[BUFSIZ];
};

static void end(void *state) {
	struct jxl_state *js = state;
	JxlEncoderDestroy(js->enc);
}

static bool process_output(struct jxl_state *js, FILE *ofp) {
	JxlEncoderStatus st;
	do {
		uint8_t *out = js->buf;
		size_t size = sizeof(js->buf);
		st = JxlEncoderProcessOutput(js->enc, &out, &size);
		if (st == JXL_ENC_ERROR) {
			break;
		}
		fwrite(js->buf, 1, sizeof(js->buf) - size, ofp);
	} while (st == JXL_ENC_NEED_MORE_OUTPUT);
	return st == JXL_ENC_SUCCESS;
}

size_t write_frame(void *state, const struct wuimg *dst, FILE *ofp,
const int frame) {
	struct jxl_state *js = state;
	bool final = true;
	if (js->frames) {
		final = (size_t)(frame + 1) == js->frames->nr;
		const struct frame_time *sec = &js->frames->f[frame].sec;
		JxlFrameHeader header;
		JxlEncoderInitFrameHeader(&header);
		if (js->round_time) {
			header.duration = (uint32_t)lroundf(fclampf(
				(float)sec->num * APPROX_TIME_RES / (float)sec->den,
				0, (float)(UINT32_MAX)
			));
		} else {
			header.duration = sec->num;
		}
		JxlEncoderSetFrameHeader(js->settings, &header);
	}
	JxlEncoderSetFrameBitDepth(js->settings, &js->depth);
	JxlEncoderAddImageFrame(js->settings, &js->fmt, dst->data, wuimg_size(dst));
	if (final) {
		JxlEncoderCloseInput(js->enc);
	}
	return process_output(js, ofp);
}

static uint32_t same_time_res(const struct image_frames *frames) {
	const struct frame_info *info = frames->f;
	const uint32_t den = info[0].sec.den;
	for (size_t i = 1; i < frames->nr; ++i) {
		if (info[i].sec.den != den) {
			return 0;
		}
	}
	return den;
}

static int get_transfer(const struct wuimg *img, double *gamma) {
	const double g = color_space_get_gamma(&img->cs);
	if (g != 0.0) {
		if (gamma) {
			*gamma = 1.0/g;
		}
		return JXL_TRANSFER_FUNCTION_GAMMA;
	}
	switch (img->cs.transfer) {
	case cicp_transfer_iec_61966_2_1: return JXL_TRANSFER_FUNCTION_SRGB;
	case cicp_transfer_bt709_6: return JXL_TRANSFER_FUNCTION_709;
	case cicp_transfer_unspecified: return JXL_TRANSFER_FUNCTION_UNKNOWN;
	case cicp_transfer_linear: return JXL_TRANSFER_FUNCTION_LINEAR;
	case cicp_transfer_smpte_st_2084: return JXL_TRANSFER_FUNCTION_PQ;
	case cicp_transfer_smpte_st_428_1: return JXL_TRANSFER_FUNCTION_DCI;
	case cicp_transfer_arib_std_b67: return JXL_TRANSFER_FUNCTION_HLG;
	default:
		if (img->cs.transfer == 0) {
			return JXL_TRANSFER_FUNCTION_SRGB;
		}
	}
	return NO_TRANSFER;
}

static JxlPrimaries get_primaries(const struct wuimg *img) {
	if (img->cs.type == color_profile_enum) {
		switch (img->cs.primaries) {
		case cicp_primaries_bt709_6: return JXL_PRIMARIES_SRGB;
		case cicp_primaries_bt2020_2: return JXL_PRIMARIES_2100;
		case cicp_primaries_smpte_rp_431_2: return JXL_PRIMARIES_P3;
		default:
			if (!img->cs.primaries) {
				return JXL_PRIMARIES_SRGB;
			}
			break;
		}
	}
	return JXL_PRIMARIES_CUSTOM;
}

static JxlWhitePoint get_white_point(const struct wuimg *img) {
	switch (color_space_white_point_type(&img->cs)) {
	case color_white_other:
	case color_white_c:
		break;
	case color_white_d65: return JXL_WHITE_POINT_D65;
	case color_white_e: return JXL_WHITE_POINT_E;
	case color_white_dci: return JXL_WHITE_POINT_DCI;
	}
	if (!img->cs.primaries) {
		return JXL_WHITE_POINT_D65;
	}
	return JXL_WHITE_POINT_CUSTOM;
}

static JxlOrientation get_orientation(const struct wuimg *img) {
	switch (img->mirror << 2 | img->rotate) {
	case 1: return JXL_ORIENT_ROTATE_90_CW;
	case 2: return JXL_ORIENT_ROTATE_180;
	case 3: return JXL_ORIENT_ROTATE_90_CCW;
	case 4: return JXL_ORIENT_FLIP_VERTICAL;
	case 5: return JXL_ORIENT_ANTI_TRANSPOSE;
	case 6: return JXL_ORIENT_FLIP_HORIZONTAL;
	case 7: return JXL_ORIENT_TRANSPOSE;
	}
	return JXL_ORIENT_IDENTITY;
}

static const char * init_jxl(void *state, const struct wuimg *dst,
const struct wuimg *src, FILE *ofp) {
	struct jxl_state *js = state;
	js->enc = JxlEncoderCreate(NULL);
	if (!js->enc) {
		return "couldn't create JXL encoder";
	}
	js->settings = JxlEncoderFrameSettingsCreate(js->enc, NULL);
	if (!js->settings) {
		return "couldn't create JXL frame settings struct";
	}

	js->fmt = (JxlPixelFormat) {
		.num_channels = dst->channels,
		.data_type = dst->attr == pix_float
			? (dst->bitdepth > 16 ? JXL_TYPE_FLOAT : JXL_TYPE_FLOAT16)
			: (dst->bitdepth > 8 ? JXL_TYPE_UINT16 : JXL_TYPE_UINT8),
		.endianness = JXL_NATIVE_ENDIAN,
		.align = 1 << dst->align_sh,
	};
	js->depth = (JxlBitDepth) {
		.type = dst->attr == pix_float
			? JXL_BIT_DEPTH_FROM_PIXEL_FORMAT
			: JXL_BIT_DEPTH_FROM_CODESTREAM,
	};
	js->frames = src->frames;
	js->round_time = false;

	JxlBasicInfo info;
	JxlEncoderInitBasicInfo(&info);
	info.xsize = (uint32_t)dst->w;
	info.ysize = (uint32_t)dst->h;
	info.bits_per_sample = dst->bitrange;
	info.exponent_bits_per_sample = dst->attr == pix_float
		? (dst->bitdepth > 16 ? 8 : 5)
		: 0;
	info.num_color_channels = (dst->channels >= 3) ? 3 : 1;
	info.num_extra_channels = (dst->channels & 1) ? 0 : 1;
	info.alpha_bits = (dst->channels & 1) ? 0 : info.bits_per_sample;
	info.alpha_exponent_bits = (dst->channels & 1)
		? 0 : info.exponent_bits_per_sample;
	info.alpha_premultiplied = dst->alpha == alpha_associated;
	info.uses_original_profile = JXL_TRUE;
	info.orientation = get_orientation(dst);
	if (js->frames) {
		info.have_animation = JXL_TRUE;
		info.animation.tps_denominator = 1;
		info.animation.tps_numerator = same_time_res(js->frames);
		if (!info.animation.tps_numerator) {
			info.animation.tps_numerator = APPROX_TIME_RES;
			js->round_time = true;
		}
	}
	if (JxlEncoderSetBasicInfo(js->enc, &info) != JXL_ENC_SUCCESS) {
		return "couldn't set basic info";
	}

	if (dst->cs.type == color_profile_icc) {
		const struct wuptr icc = icc_file_get_data(dst->cs.desc.icc);
		if (JxlEncoderSetICCProfile(js->enc, icc.ptr, icc.len)
		!= JXL_ENC_SUCCESS) {
			return "couldn't set ICC profile";
		}
	} else {
		JxlColorEncoding color;
		JxlColorEncodingSetToSRGB(&color, dst->channels < 3);
		const struct color_primaries *pri = color_space_get_primaries(&dst->cs);
		if (pri) {
			color.white_point = get_white_point(dst);
			if (color.white_point == JXL_WHITE_POINT_CUSTOM) {
				color.white_point_xy[0] = pri->w.x;
				color.white_point_xy[1] = pri->w.y;
			}
			color.primaries = get_primaries(dst);
			if (color.primaries == JXL_PRIMARIES_CUSTOM) {
				color.primaries_red_xy[0] = pri->r.x;
				color.primaries_red_xy[1] = pri->r.y;
				color.primaries_green_xy[0] = pri->g.x;
				color.primaries_green_xy[1] = pri->g.y;
				color.primaries_blue_xy[0] = pri->b.x;
				color.primaries_blue_xy[1] = pri->b.y;
			}
		}
		const int transfer = get_transfer(dst, &color.gamma);
		if (transfer != NO_TRANSFER) {
			color.transfer_function = (JxlTransferFunction)transfer;
		}
		if (JxlEncoderSetColorEncoding(js->enc, &color) != JXL_ENC_SUCCESS) {
			return "couldn't set color encoding";
		}
	}

	JxlEncoderSetFrameLossless(js->settings, JXL_TRUE);
	const bool compress_better = false;
	JxlEncoderFrameSettingsSetOption(js->settings, JXL_ENC_FRAME_SETTING_EFFORT,
		compress_better ? 10 : 1);
	JxlEncoderFrameSettingsSetOption(js->settings, JXL_ENC_FRAME_SETTING_DECODING_SPEED,
		compress_better ? 0 : 4);
	JxlEncoderFrameSettingsSetOption(js->settings, JXL_ENC_FRAME_SETTING_BROTLI_EFFORT,
		compress_better ? 11 : 0);
	return process_output(js, ofp) ? NULL : "couldn't write header";
}

static bool passthrough(const struct wuimg *src) {
	switch (src->attr) {
	case pix_normal:
		switch (src->bitdepth) {
		case 8: case 16: break;
		default: return false;
		}
		break;
	case pix_float:
		switch (src->bitdepth) {
		case 16: case 32: break;
		default: return false;
		}
		break;
	default: return false;
	}

	if (src->alpha == alpha_key) {
		return false;
	} else if (src->alpha == alpha_ignore && (src->channels & 1) == 0) {
		return false;
	}

	enum pix_layout l_expect;
	switch (src->channels) {
	case 1: case 2: l_expect = pix_gray; break;
	case 3: case 4: l_expect = pix_rgba; break;
	default: return false;
	}

	const struct color_space *cs = &src->cs;
	if (src->align_sh != align_bitpack && src->layout == l_expect
	&& cs->matrix == cicp_matrix_rgb) {
		switch (cs->type) {
		case color_profile_enum:
		case color_profile_param:
			return cs->limited == false
				&& get_transfer(src, NULL) != NO_TRANSFER;
		case color_profile_icc:
			return true;
		}
	}
	return false;
}

static bool best_fit(struct wuimg *dst, const struct wuimg *src) {
	dst->w = (src->rotate & 1) ? src->h : src->w;
	dst->h = (src->rotate & 1) ? src->w : src->h;
	switch (src->mode) {
	case image_mode_raw:
	case image_mode_planar:
		dst->channels = src->channels;
		dst->bitdepth = src->bitdepth > 8 ? 16 : 8;
		if (src->mode == image_mode_raw) {
			return passthrough(src);
		}
		break;
	case image_mode_palette:
		dst->channels = 4;
		dst->bitdepth = 8;
		break;
	case image_mode_bitfield:
		dst->channels = src->u.bitfield->ch;
		dst->bitdepth = src->u.bitfield->outdepth;
		break;
	}
	return false;
}

const struct enc_fn jpegxl_enc = {
	.state_size = sizeof(struct jxl_state),
	.anim = true,
	.best_fit = best_fit,
	.init = init_jxl,
	.write_frame = write_frame,
	.end = end,
};

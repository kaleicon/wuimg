#include <jxl/decode.h>

#include "../wudefs.h"

static void set_colorspace(struct raw_img *img, JxlDecoder *jd) {
	const JxlColorProfileTarget target = JXL_COLOR_PROFILE_TARGET_ORIGINAL;
	JxlColorEncoding enc;
	if (JxlDecoderGetColorAsEncodedProfile(jd, NULL, target, &enc)
	== JXL_DEC_SUCCESS) {
		switch (enc.color_space) {
		case JXL_COLOR_SPACE_RGB:
		case JXL_COLOR_SPACE_GRAY:
			color_space_set_primaries(&img->cs,
				enc.white_point_xy[0],
				enc.white_point_xy[1],
				enc.primaries_red_xy[0],
				enc.primaries_red_xy[1],
				enc.primaries_green_xy[0],
				enc.primaries_green_xy[1],
				enc.primaries_blue_xy[0],
				enc.primaries_blue_xy[1]);
			if (enc.transfer_function == JXL_TRANSFER_FUNCTION_GAMMA) {
				color_space_set_gamma(&img->cs, enc.gamma);
			} else {
				img->cs.transfer = enc.transfer_function;
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

static bool set_fmt(const struct raw_img *img, JxlPixelFormat *fmt) {
	fmt->num_channels = img->channels;
	fmt->align = img->alignment;
	switch (img->bitdepth) {
	case 8: fmt->data_type = JXL_TYPE_UINT8; break;
	case 16:
		fmt->data_type = (img->attr == pix_float)
			? JXL_TYPE_FLOAT16 : JXL_TYPE_UINT16;
		break;
	case 32:
		fmt->data_type = (img->attr == pix_float)
			? JXL_TYPE_FLOAT : JXL_TYPE_UINT32;
		break;
	default:
		return false;
	}
	return true;
}

static enum wu_error dec_wrap(struct image_file *infile,
const struct wu_conf *wuconf, JxlDecoder *jd) {
	JxlDecoderSetKeepOrientation(jd, JXL_TRUE);
	JxlDecoderSubscribeEvents(jd, JXL_DEC_BASIC_INFO
		| JXL_DEC_COLOR_ENCODING
		| JXL_DEC_JPEG_RECONSTRUCTION
		| JXL_DEC_FULL_IMAGE);

	JxlPixelFormat fmt;
	for (;;) {
		switch (JxlDecoderProcessInput(jd)) {
		case JXL_DEC_BASIC_INFO:
			;JxlBasicInfo info;
			if (JxlDecoderGetBasicInfo(jd, &info) != JXL_DEC_SUCCESS
			|| JxlDecoderDefaultPixelFormat(jd, &fmt) != JXL_DEC_SUCCESS) {
				return wu_invalid_header;
			}
			if (info.have_animation) {
				return wu_unsupported_feature;
			}

			struct raw_img *img = alloc_sub_images(infile, 1);
			if (!img) {
				return wu_alloc_error;
			}

			img->w = info.xsize;
			img->h = info.ysize;
			if (raw_img_exceeds_limit(img, wuconf)) {
				return wu_exceeds_size_limit;
			}
			img->channels = (uint8_t)(info.num_color_channels
				+ (bool)info.alpha_bits);
			img->bitdepth = (uint8_t)info.bits_per_sample;
			if (img->bitdepth > 8 && info.exponent_bits_per_sample) {
				img->attr = pix_float;
			}
			img->alpha = info.alpha_premultiplied
				? alpha_associated : alpha_unassociated;
			raw_img_exif_orientation(img, info.orientation);
			if (!set_fmt(img, &fmt)) {
				return wu_unsupported_feature;
			}
			break;
		case JXL_DEC_COLOR_ENCODING:
			set_colorspace(img, jd);
			break;
		case JXL_DEC_JPEG_RECONSTRUCTION:
			tree_bud_leaf(&infile->metadata, "JPEG source",
				(struct wu_leaf){.val.b = true, .type = wu_leaf_bool});
			break;
		case JXL_DEC_NEED_IMAGE_OUT_BUFFER:
			;const enum wu_error st = raw_img_alloc(img);
			if (st != wu_ok) {
				return st;
			}
			if (JxlDecoderSetImageOutBuffer(jd, &fmt, img->data,
			raw_img_size(img)) != JXL_DEC_SUCCESS) {
				return wu_invalid_params;
			}
			break;
		case JXL_DEC_FULL_IMAGE:
			return wu_ok;
		default:
			return wu_invalid_params;
		}
	}
	return wu_invalid_params;
}

enum wu_error jpegxl_dec(struct image_file *infile,
const struct wu_conf *wuconf) {
	struct map_info mm;
	enum wu_error err = wu_ok;
	if (map_file(&mm, infile->ifp)) {
		JxlDecoder *jd = JxlDecoderCreate(NULL);
		if (jd) {
			/* libjxl can use threading, but the default
			 * implementations (all of them) make decoding slower
			 * on my 4-core machine, so disabled they remain. */
			JxlDecoderSetInput(jd, mm.data, mm.len);
			JxlDecoderCloseInput(jd);
			err = dec_wrap(infile, wuconf, jd);
			JxlDecoderDestroy(jd);
		} else {
			err = wu_alloc_error;
		}
		unmap_file(&mm);
	} else {
		err = wu_alloc_error;
	}
	return err;
}

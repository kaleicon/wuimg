#include <charls/charls_jpegls_decoder.h>

#include "rast_utils.h"
#include "raster/strip.h"

static void comment_handler(const void *data, const size_t size, void *ptr) {
	tree_add_leaf_len(ptr, "Comment", data, size, NULL);
}

static enum wu_error read_data(struct image_file *infile,
const struct wu_conf *wuconf, charls_jpegls_decoder *dec, charls_jpegls_errc *err) {
	int found;
	charls_spiff_header spiff;
	*err = charls_jpegls_decoder_read_spiff_header(dec, &spiff, &found);
	if (*err != CHARLS_JPEGLS_ERRC_SUCCESS) {
		return wu_invalid_header;
	}

	*err = charls_jpegls_decoder_read_header(dec);
	if (*err != CHARLS_JPEGLS_ERRC_SUCCESS) {
		return wu_invalid_header;
	}

	charls_frame_info frame;
	*err = charls_jpegls_decoder_get_frame_info(dec, &frame);
	if (*err != CHARLS_JPEGLS_ERRC_SUCCESS) {
		return wu_decoding_error;
	}

	charls_interleave_mode mode;
	*err = charls_jpegls_decoder_get_interleave_mode(dec, &mode);
	if (*err != CHARLS_JPEGLS_ERRC_SUCCESS) {
		return wu_decoding_error;
	}

	struct wuimg *img = alloc_sub_images(infile, 1);
	if (!img) {
		return wu_alloc_error;
	}

	img->w = frame.width;
	img->h = frame.height;
	if (wuimg_exceeds_limit(img, wuconf)) {
		return wu_exceeds_size_limit;
	} else if (frame.component_count > 4 || frame.bits_per_sample > 16) {
		return wu_unsupported_feature;
	}
	img->channels = (uint8_t)frame.component_count;
	img->bitdepth = (frame.bits_per_sample > 8) ? 16 : 8;
	img->used_bits = (uint8_t)frame.bits_per_sample;
	tree_bud_leaf(&infile->metadata, "Bitdepth",
		(struct wu_leaf) {.type = wu_leaf_signed, .val.d = frame.bits_per_sample});

	switch (mode) {
	case CHARLS_INTERLEAVE_MODE_NONE:
		wuimg_plane_init(img);
		break;
	case CHARLS_INTERLEAVE_MODE_LINE:
		return wu_unsupported_feature;
	case CHARLS_INTERLEAVE_MODE_SAMPLE:
		break;
	default:
		return wu_invalid_params;
	}

	const enum wu_error st = wuimg_alloc(img);
	if (st != wu_ok) {
		return st;
	}

	const size_t size = wuimg_size(img);
	*err = charls_jpegls_decoder_decode_to_buffer(dec, img->data, size, 0);
	if (*err != CHARLS_JPEGLS_ERRC_SUCCESS) {
		return wu_decoding_error;
	}
	return wu_ok;
}

static enum wu_error dec_wrap(struct image_file *infile,
const struct wu_conf *wuconf, const struct map_info *mm) {
	enum wu_error st = wu_alloc_error;
	charls_jpegls_decoder *dec = charls_jpegls_decoder_create();
	if (dec) {
		// Ignore result.
		charls_jpegls_errc err = charls_jpegls_decoder_at_comment(dec,
			comment_handler, &infile->metadata);

		err = charls_jpegls_decoder_set_source_buffer(dec, mm->data,
			mm->len);
		if (err == CHARLS_JPEGLS_ERRC_SUCCESS) {
			st = read_data(infile, wuconf, dec, &err);
		}

		if (err != CHARLS_JPEGLS_ERRC_SUCCESS) {
			image_file_error_append(infile,
				charls_get_error_message(err));
		}
		charls_jpegls_decoder_destroy(dec);
	}
	return st;
}

enum wu_error jpegls_dec(struct image_file *infile,
const struct wu_conf *wuconf) {
	return rast_map_wrap(infile, wuconf, dec_wrap);
}

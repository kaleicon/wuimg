// SPDX-License-Identifier: 0BSD
#include <charls/charls.h>

#include "wudefs.h"

static const char * compression_str(const charls_spiff_compression_type comp) {
	switch (comp) {
	case CHARLS_SPIFF_COMPRESSION_TYPE_UNCOMPRESSED: return "Uncompressed";
	case CHARLS_SPIFF_COMPRESSION_TYPE_MODIFIED_HUFFMAN: return "Modified Huffman";
	case CHARLS_SPIFF_COMPRESSION_TYPE_MODIFIED_READ: return "Modified Read";
	case CHARLS_SPIFF_COMPRESSION_TYPE_MODIFIED_MODIFIED_READ: return "Modified Modified Read";
	case CHARLS_SPIFF_COMPRESSION_TYPE_JBIG: return "JBIG";
	case CHARLS_SPIFF_COMPRESSION_TYPE_JPEG: return "JPEG";
	case CHARLS_SPIFF_COMPRESSION_TYPE_JPEG_LS: return "JPEG LS";
	}
	return "???";
}

static const char * interleave_str(const charls_interleave_mode mode) {
	switch (mode) {
	case CHARLS_INTERLEAVE_MODE_NONE: return "None";
	case CHARLS_INTERLEAVE_MODE_LINE: return "Line";
	case CHARLS_INTERLEAVE_MODE_SAMPLE: return "Sample";
	}
	return "???";
}

static int comment_handler(const void *data, const size_t size, void *ptr) {
	tree_add_leaf_len(ptr, "Comment", wuptr_mem(data, size), NULL);
	return 0;
}

static enum wu_error read_data(struct image_file *infile,
const struct wu_conf *wuconf, charls_jpegls_decoder *dec, charls_jpegls_errc *err) {
	int32_t found;
	charls_spiff_header spiff;
	*err = charls_jpegls_decoder_read_spiff_header(dec, &spiff, &found);
	if (*err != CHARLS_JPEGLS_ERRC_SUCCESS) {
		return wu_invalid_header;
	}
	if (found) {
		tree_bud_leaf_d(&infile->metadata, "Colorspace", spiff.color_space);
		tree_add_leaf_utf8(&infile->metadata, "Compression",
			compression_str(spiff.compression_type));
		// TODO: Interpret and report resolution
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
#if CHARLS_VERSION_MAJOR > 2
	// FIXME: Untested!
	*err = charls_jpegls_decoder_get_interleave_mode(dec, 0 /* ??? */, &mode);
#else
	*err = charls_jpegls_decoder_get_interleave_mode(dec, &mode);
#endif
	if (*err != CHARLS_JPEGLS_ERRC_SUCCESS) {
		return wu_decoding_error;
	}

	tree_add_leaf_utf8(&infile->metadata, "Interleave", interleave_str(mode));

	struct wuimg *img = infile->sub_img;
	img->w = frame.width;
	img->h = frame.height;
	if (wuimg_exceeds_limit(img, wuconf)) {
		return wu_exceeds_size_limit;
	} else if (frame.component_count > 4) {
		return wu_unsupported_feature;
	}
	img->channels = (uint8_t)frame.component_count;
	img->bitdepth = (frame.bits_per_sample > 8) ? 16 : 8;
	img->used_bits = (uint8_t)frame.bits_per_sample;
	if (mode == CHARLS_INTERLEAVE_MODE_NONE) {
		wuimg_plane_init(img);
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

	int32_t near;
	if (charls_jpegls_decoder_get_near_lossless(dec, 0, &near) == CHARLS_JPEGLS_ERRC_SUCCESS) {
		tree_bud_leaf_d(&infile->metadata, "NEAR", near);
	}
	return wu_ok;
}

static enum wu_error jpegls_dec(struct image_file *infile,
const struct wu_conf *wuconf) {
	enum wu_error st = wu_alloc_error;
	charls_jpegls_decoder *dec = charls_jpegls_decoder_create();
	if (dec) {
		// Result can't be ignored here
		charls_jpegls_errc err = charls_jpegls_decoder_at_comment(dec,
			comment_handler, &infile->metadata);

		err = charls_jpegls_decoder_set_source_buffer(dec,
			infile->map.ptr, infile->map.len);
		if (err == CHARLS_JPEGLS_ERRC_SUCCESS) {
			st = read_data(infile, wuconf, dec, &err);
		}

		if (err != CHARLS_JPEGLS_ERRC_SUCCESS) {
			image_file_strerror_append(infile,
				charls_get_error_message(err));
		}
		charls_jpegls_decoder_destroy(dec);
	}
	return st;
}

const struct image_fn jpegls_fn = {
	.mmap = true,
	.alloc_single = true,
	.dec = jpegls_dec
};

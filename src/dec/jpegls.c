// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2022 kaleido
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

static int handle_jls_comment(const void *data, const size_t size, void *ptr) {
	tree_add_leaf_len(ptr, "Comment", wuptr_mem(data, size), NULL);
	return 0;
}

static struct wu_st read_jls_data(struct image_file *infile,
charls_jpegls_decoder *dec, charls_jpegls_errc *err) {
	*err = charls_jpegls_decoder_set_source_buffer(dec,
		infile->map.ptr, infile->map.len);
	if (*err != CHARLS_JPEGLS_ERRC_SUCCESS) {
		return WUERR_HERE(wu_invalid_header);
	}

	int32_t has_spiff;
	charls_spiff_header spiff;
	*err = charls_jpegls_decoder_read_spiff_header(dec, &spiff, &has_spiff);
	if (*err != CHARLS_JPEGLS_ERRC_SUCCESS) {
		return WUERR_HERE(wu_invalid_header);
	}
	if (has_spiff) {
		tree_bud_leaf_d(&infile->metadata, "Colorspace", spiff.color_space);
		tree_add_leaf_utf8(&infile->metadata, "Compression",
			compression_str(spiff.compression_type));
		// TODO: Interpret and report resolution
	}

	*err = charls_jpegls_decoder_read_header(dec);
	if (*err != CHARLS_JPEGLS_ERRC_SUCCESS) {
		return WUERR_HERE(wu_invalid_header);
	}

	charls_frame_info frame;
	*err = charls_jpegls_decoder_get_frame_info(dec, &frame);
	if (*err != CHARLS_JPEGLS_ERRC_SUCCESS) {
		return WUERR_HERE(wu_decoding_error);
	} else if (frame.component_count > UINT8_MAX) {
		return WUERR_HERE(wu_unsupported_feature);
	}


	charls_interleave_mode mode;
#if CHARLS_VERSION_MAJOR > 2
	// FIXME: Untested!
	*err = charls_jpegls_decoder_get_interleave_mode(dec, 0 /* ??? */, &mode);
#else
	*err = charls_jpegls_decoder_get_interleave_mode(dec, &mode);
#endif
	if (*err != CHARLS_JPEGLS_ERRC_SUCCESS) {
		return WUERR_HERE(wu_decoding_error);
	}

	tree_add_leaf_utf8(&infile->metadata, "Interleave", interleave_str(mode));

	struct wuimg *img = infile->sub_img;
	img->w = frame.width;
	img->h = frame.height;
	img->channels = (uint8_t)frame.component_count;
	img->bitdepth = (frame.bits_per_sample > 8) ? 16 : 8;
	img->bitrange = (uint8_t)frame.bits_per_sample;
	if (mode == CHARLS_INTERLEAVE_MODE_NONE) {
		wuimg_plane_init(img);
	}
	return WU_OK;
}

static void end_jpegls(struct image_file *infile) {
	charls_jpegls_decoder_destroy(infile->dec_state);
}

static struct wu_st event_jpegls(struct image_file *infile,
struct wu_state *state, const enum image_event ev) {
	(void)state;
	charls_jpegls_errc err;
	charls_jpegls_decoder *dec = infile->dec_state;
	struct wu_st st = WU_NO_CHANGE;
	switch (ev) {
	case ev_metadata:
		st = read_jls_data(infile, dec, &err);
		break;
	case ev_subcycle:
		err = charls_jpegls_decoder_decode_to_buffer(dec,
			infile->sub_img->data, wuimg_size(infile->sub_img), 0);
		if (err == CHARLS_JPEGLS_ERRC_SUCCESS) {
			st = WU_OK;

			int32_t near;
			if (charls_jpegls_decoder_get_near_lossless(dec, 0, &near)
			== CHARLS_JPEGLS_ERRC_SUCCESS) {
				tree_bud_leaf_d(&infile->metadata, "NEAR", near);
			}
		} else {
			st = WUERR_HERE(wu_decoding_error);
		}
		break;
	default: return st;
	}
	if (err != CHARLS_JPEGLS_ERRC_SUCCESS) {
		image_file_strerror_append(infile,
			charls_get_error_message(err));
	}
	return st;
}

static struct wu_st init_jpegls(struct image_file *infile) {
	charls_jpegls_decoder *dec = charls_jpegls_decoder_create();
	if (dec) {
		infile->dec_state = dec;
		charls_jpegls_errc err = charls_jpegls_decoder_at_comment(dec,
			handle_jls_comment, &infile->metadata);
		// Silence compiler complaints about unused result
		(void)err;
		return WU_OK;
	}
	return WUERR_HERE(wu_alloc_error);
}

const struct image_fn jpegls_fn = {
	.mmap = true,
	.alloc_single = true,
	.alloc_on_subcycle = true,
	.init = init_jpegls,
	.event = event_jpegls,
	.end = end_jpegls,
};

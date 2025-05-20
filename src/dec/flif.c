// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2019 kaleido
#include <flif.h>

#include "wudefs.h"
#include "misc/common.h"
#include "misc/metadata.h"

struct flif_state {
	FLIF_DECODER *dec;
	void (*read_func)(FLIF_IMAGE *image, uint32_t row, void *buffer,
		size_t buffer_size_bytes);
};

static void flif_end(struct image_file *infile) {
	struct flif_state *ds = infile->dec_state;
	flif_destroy_decoder(ds->dec);
}

static enum wu_error decode_frame(struct wuimg *img, FLIF_IMAGE *frame,
struct flif_state *ds) {
	const size_t stride = wuimg_stride(img);
	for (uint32_t y = 0; y < img->h; ++y) {
		ds->read_func(frame, y, img->data + y*stride, stride);
	}
	return wu_ok;
}

static enum wu_error flif_callback(struct image_file *infile,
const struct wu_conf *wuconf, struct wu_state *state, const enum image_event ev) {
	(void)wuconf; (void)ev;
	struct flif_state *ds = infile->dec_state;
	FLIF_IMAGE *frame = flif_decoder_get_image(ds->dec,
		(size_t)state->frame);
	return (ev == ev_frame)
		? decode_frame(infile->sub_img, frame, ds)
		: wu_no_change;
}

static void read_metadata(struct wutree *tree, FLIF_IMAGE *frame) {
	struct {
		const char *name;
		enum metadata_type type;
	} chunks[] = {
		{"eXif", metadata_exif},
		{"eXmp", metadata_xmp},
	};
	for (size_t i = 0; i < ARRAY_LEN(chunks); ++i) {
		unsigned char *data = NULL;
		size_t len;
		flif_image_get_metadata(frame, chunks[i].name, &data, &len);
		if (data && len) {
			metadata_parse(chunks[i].type, data, len, tree);
		}
	}
}

static enum wu_error setup_img(struct image_file *infile,
const struct wu_conf *wuconf, struct flif_state *ds) {
	struct wuimg *img = infile->sub_img;
	FLIF_IMAGE *frame = flif_decoder_get_image(ds->dec, 0);
	img->w = flif_image_get_width(frame);
	img->h = flif_image_get_height(frame);
	if (wuimg_exceeds_limit(img, wuconf)) {
		return wu_exceeds_size_limit;
	}
	img->channels = flif_image_get_nb_channels(frame);
	img->bitdepth = flif_image_get_depth(frame);
	img->alpha = alpha_unassociated;

	if (img->channels == 1 && flif_image_get_palette_size(frame)) {
		wuimg_palette_init(img);
	}

	if (img->mode == image_mode_palette) {
		flif_image_get_palette(frame, img->u.palette);
		ds->read_func = flif_image_read_row_PALETTE8;
	} else if (img->channels == 1) {
		ds->read_func = flif_image_read_row_GRAY8;
	} else {
		if (img->channels == 3) {
			// There are no RGB functions.
			img->channels = 4;
			img->alpha = alpha_ignore;
		}
		ds->read_func = (img->bitdepth == 8)
			? flif_image_read_row_RGBA8
			: flif_image_read_row_RGBA16;
	}

	const enum wu_error st = wuimg_alloc(img);
	if (st != wu_ok) {
		return st;
	}

	const size_t nr = flif_decoder_num_images(ds->dec);
	if (nr > 1) {
		if (!wuimg_frames_init(img, nr)) {
			return wu_alloc_error;
		}
		for (size_t i = 0; i < nr; ++i) {
			wuimg_frame_set(img, i, 0, 0, img->w, img->h,
				flif_image_get_frame_delay(frame), 1000, true);
		}
	}

	read_metadata(&infile->metadata, frame);
	return decode_frame(img, frame, ds);
}

static enum wu_error flif_dec(struct image_file *infile,
const struct wu_conf *wuconf) {
	struct flif_state *ds = infile->dec_state;
	ds->dec = flif_create_decoder();
	const int32_t success = flif_decoder_decode_memory(ds->dec,
		infile->map.ptr, infile->map.len);
	if (success) {
		return setup_img(infile, wuconf, ds);
	}
	return wu_decoding_error;
}

const struct image_fn flif_fn = {
	.mmap = true,
	.alloc_single = true,
	.state_size = sizeof(struct flif_state),
	.dec = flif_dec,
	.callback = flif_callback,
	.end = flif_end,
};

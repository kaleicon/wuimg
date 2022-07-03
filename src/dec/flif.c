#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <flif.h>

#include "../wudefs.h"
#include "../common.h"
#include "../metadata.h"

struct flif_state {
	struct map_info map;
	FLIF_DECODER *dec;
	void (*read_func)(FLIF_IMAGE *image, uint32_t row, void *buffer,
		size_t buffer_size_bytes);
};

static void clean_flif_state(struct image_file *infile) {
	struct flif_state *ds = infile->dec_state;
	if (ds->dec) {
		flif_destroy_decoder(ds->dec);
	}
	if (ds->map.data) {
		unmap_file(&ds->map);
	}
}

static enum wu_error decode_frame(struct raw_img *img, FLIF_IMAGE *frame,
struct flif_state *ds) {
	const size_t stride = raw_img_stride(img);
	for (uint32_t y = 0; y < img->h; ++y) {
		unsigned char *data = img->data + y * stride;
		ds->read_func(frame, y, data, stride);
	}
	return wu_ok;
}

enum wu_error flif_callback(struct image_file *infile,
const struct wu_conf *wuconf, struct wu_state *state, const enum image_event ev) {
	(void)wuconf;
	if (ev == ev_frame) {
		struct flif_state *ds = infile->dec_state;
		FLIF_IMAGE *frame = flif_decoder_get_image(ds->dec,
			(size_t)state->frame);
		return decode_frame(infile->sub_img, frame, ds);
	}
	clean_flif_state(infile);
	return wu_no_change;
}

static void read_metadata(struct wu_tree *tree, FLIF_IMAGE *frame) {
	struct {
		const char *name;
		enum metadata_type type;
	} chunks[] = {
		{"eXif", exif_metadata},
		{"eXmp", xmp_metadata},
	};
	for (size_t i = 0; i < ARRAY_LEN(chunks); ++i) {
		unsigned char *data = NULL;
		size_t len;
		flif_image_get_metadata(frame, chunks[i].name, &data, &len);
		if (data && len) {
			standard_metadata(chunks[i].type, data, len, tree);
		}
	}
}

static enum wu_error setup_img(struct image_file *infile,
const struct wu_conf *wuconf, struct flif_state *ds) {
	struct raw_img *img = alloc_sub_images(infile, 1);
	if (!img) {
		return wu_alloc_error;
	}

	FLIF_IMAGE *frame = flif_decoder_get_image(ds->dec, 0);
	img->w = flif_image_get_width(frame);
	img->h = flif_image_get_height(frame);
	if (zumax(img->w, img->h) > wuconf->max_img_size) {
		return wu_exceeds_size_limit;
	}
	img->channels = flif_image_get_nb_channels(frame);
	img->bitdepth = flif_image_get_depth(frame);
	img->alpha = alpha_unassociated;

	if (img->channels == 1 && flif_image_get_palette_size(frame)) {
		raw_img_set_palette(img, malloc(sizeof(*img->u.palette)));
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

	const enum wu_error st = raw_img_alloc(img);
	if (st != wu_ok) {
		return st;
	}

	const size_t nr = flif_decoder_num_images(ds->dec);
	if (nr > 1) {
		struct image_frames *f = raw_img_frames_init(img, nr);
		if (!f) {
			return wu_alloc_error;
		}
		for (size_t i = 0; i < f->nr; ++i) {
			f->f[i] = (struct frame_info) {
				.w = img->w,
				.h = img->h,
				.msec = (int)flif_image_get_frame_delay(frame),
			};
		}
		infile->events = ev_frame;
	}

	read_metadata(&infile->metadata, frame);
	return decode_frame(img, frame, ds);
}

enum wu_error flif_dec(struct image_file *infile,
const struct wu_conf *wuconf) {
	struct flif_state *ds = calloc(1, sizeof(*ds));
	if (!ds) {
		return wu_alloc_error;
	}
	infile->dec_state = ds;

	if (!map_file(&ds->map, infile->ifp)) {
		return wu_alloc_error;
	}

	ds->dec = flif_create_decoder();
	const int32_t success = flif_decoder_decode_memory(ds->dec,
		ds->map.data, ds->map.len);
	if (success) {
		return setup_img(infile, wuconf, ds);
	}
	return wu_decoding_error;
}

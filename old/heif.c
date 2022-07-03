#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include <libheif/heif.h>

#include "../wudefs.h"
#include "../common.h"
#include "../metadata.h"

struct heif_state {
	struct map_info mm;
	struct heif_context *ctx;
	struct heif_decoding_options *opts;
	heif_item_id *hids;
	struct heif_image **himgs;
	heif_item_id primary_id;
};

static void clean_heif_state(struct image_file *infile) {
	struct heif_state *ds = infile->dec_state;
	if (ds->himgs) {
		for (size_t i = 0; i < infile->nr; ++i) {
			if (ds->himgs[i]) {
				heif_image_release(ds->himgs[i]);
			}
			infile->sub_img[i].data = NULL;
		}
		free(ds->himgs);
	}
	free(ds->hids);
	if (ds->opts) {
		heif_decoding_options_free(ds->opts);
	}
	if (ds->ctx) {
		heif_context_free(ds->ctx);
	}
	if (ds->mm.data) {
		unmap_file(&ds->mm);
	}
	free(ds);
	infile->dec_state = NULL;
	infile->events = 0;
}

static void scale_depth(struct raw_img *img, const int bit_range,
const int bytes_per_line) {
	const size_t len = img->w * img->channels;
	const size_t stride = (size_t)bytes_per_line / 2;

	const unsigned maxval = (1U << bit_range) - 1;
	const uint_fast32_t scale = ((unsigned)USHRT_MAX << 16) / maxval + 1;

	uint16_t *data = (uint16_t *)img->data;
	for (size_t y = 0; y < img->h; ++y) {
		for (size_t x = 0; x < len; ++x) {
			const size_t pos = y * stride + x;
			data[pos] = (uint16_t)((data[pos] * scale) >> 16);
		}
	}
}

static enum heif_chroma calc_chroma(const struct heif_image_handle *handle,
unsigned char *channels) {
	const int luma_bits = heif_image_handle_get_luma_bits_per_pixel(handle);
	const int chroma_bits = heif_image_handle_get_chroma_bits_per_pixel(handle);
	const bool high_depth = luma_bits > 8 || chroma_bits > 8;

	if (heif_image_handle_has_alpha_channel(handle)) {
		*channels = 4;
		if (high_depth) {
			switch (which_end()) {
			case big_endian:
				return heif_chroma_interleaved_RRGGBBAA_BE;
			case little_endian:
				return heif_chroma_interleaved_RRGGBBAA_LE;
			}
		}
		return heif_chroma_interleaved_RGBA;
	} else {
		*channels = 3;
		if (high_depth) {
			switch (which_end()) {
			case big_endian:
				return heif_chroma_interleaved_RRGGBB_BE;
			case little_endian:
				return heif_chroma_interleaved_RRGGBB_LE;
			}
		}
		return heif_chroma_interleaved_RGB;
	}
}

static void handle_metadata(const struct heif_image_handle* handle,
struct wu_tree *tree) {
	const int blocks = heif_image_handle_get_number_of_metadata_blocks(
		handle, NULL);
	heif_item_id *ids = malloc(sizeof(*ids) * (size_t)blocks);
	if (!ids) {
		return;
	}

	heif_image_handle_get_list_of_metadata_block_IDs(handle, NULL, ids,
		blocks);
	size_t alloc = 0;
	unsigned char *buf = NULL;
	for (int i = 0; i < blocks; ++i) {
		const size_t len = heif_image_handle_get_metadata_size(handle,
			ids[i]);
		if (len > alloc) {
			void *hold = realloc(buf, len);
			if (!hold) {
				break;
			}
			buf = hold;
			alloc = len;
		}

		const struct heif_error herr = heif_image_handle_get_metadata(
			handle, ids[i], buf);
		if (herr.code != heif_error_Ok) {
			break;
		}

		const char *type = heif_image_handle_get_metadata_type(handle,
			ids[i]);
		if (!strcmp(type, "Exif") && len > 10) {
			standard_metadata(exif_metadata, buf + 10, len - 10,
				tree);
		} else if (!strcmp(type, "mime")) {
			standard_metadata(xmp_metadata, buf, len, tree);
		} else {
			tree_sprout_leaf(tree, "Found metadata block", type);
		}
	}
	free(buf);
	free(ids);
}

static enum wu_error get_image(struct image_file *infile,
const struct wu_conf *wuconf, const size_t i) {
	struct heif_state *ds = infile->dec_state;
	struct raw_img *img = infile->sub_img + i;

	struct heif_image_handle *handle;
	struct heif_error herr = heif_context_get_image_handle(ds->ctx,
		ds->hids[i], &handle);
	if (herr.code != heif_error_Ok) {
		image_file_error_append(infile, herr.message);
		return wu_decoding_error;
	}
	if (i == 0) {
		handle_metadata(handle, &infile->metadata);
	}

	const int width = heif_image_handle_get_ispe_width(handle);
	const int height = heif_image_handle_get_ispe_height(handle);
	if ((unsigned int)imax(width, height) > wuconf->max_img_size) {
		return wu_exceeds_size_limit;
	}

	unsigned char channels;
	const enum heif_chroma chroma = calc_chroma(handle, &channels);

	herr = heif_decode_image(handle, ds->himgs + i, heif_colorspace_RGB,
		chroma, ds->opts);
	heif_image_handle_release(handle);
	if (herr.code != heif_error_Ok) {
		image_file_error_append(infile, herr.message);
		return wu_decoding_error;
	}

	if (ds->hids[i] == ds->primary_id) {
		img->id = strdup("primary");
	}

	const enum heif_channel interleaved = heif_channel_interleaved;
	int bytes_per_line;
	img->data = heif_image_get_plane(ds->himgs[i], interleaved,
		&bytes_per_line);
	img->w = (size_t)heif_image_get_width(ds->himgs[i], interleaved);
	img->h = (size_t)heif_image_get_height(ds->himgs[i], interleaved);
	img->channels = channels;
	img->bitdepth = (unsigned char)(
		heif_image_get_bits_per_pixel(ds->himgs[i], interleaved)
		/ img->channels);
	img->alignment = 16;

	const int bit_range = heif_image_get_bits_per_pixel_range(ds->himgs[i],
		interleaved);
	if (bit_range > 8 && bit_range < 16) {
		scale_depth(img, bit_range, bytes_per_line);
	}
	return wu_ok;
}

enum wu_error heif_callback(struct image_file *infile,
const struct wu_conf *wuconf, struct wu_state *state,
const enum image_event ev) {
	enum wu_error err = wu_no_change;
	if (ev == ev_subcycle && !infile->sub_img[state->idx].data) {
		err = get_image(infile, wuconf, (size_t)state->idx);
	}
	if (ev == ev_end || err > wu_ok) {
		clean_heif_state(infile);
	}
	return err;
}

enum wu_error avif_callback(struct image_file *infile,
const struct wu_conf *wuconf, struct wu_state *state,
const enum image_event ev) {
	return heif_callback(infile, wuconf, state, ev);
}

static enum wu_error dec_wrap(struct image_file *infile,
const struct wu_conf *wuconf, struct heif_state *ds) {
	if (!map_file(&ds->mm, infile->ifp)) {
		return wu_open_error;
	}

	ds->ctx = heif_context_alloc();
	struct heif_error herr = heif_context_read_from_memory_without_copy(
		ds->ctx, ds->mm.data, ds->mm.len, NULL);
	if (herr.code != heif_error_Ok) {
		image_file_error_append(infile, herr.message);
		return wu_open_error;
	}

	struct raw_img *img = alloc_sub_images(infile,
		(size_t)heif_context_get_number_of_top_level_images(ds->ctx));
	ds->hids = malloc(sizeof(*ds->hids) * infile->nr);
	ds->himgs = calloc(sizeof(*ds->himgs), infile->nr);
	if (!img || !ds->hids || !ds->himgs) {
		return wu_alloc_error;
	}
	heif_context_get_list_of_top_level_image_IDs(ds->ctx, ds->hids,
		(int)infile->nr);

	ds->opts = heif_decoding_options_alloc();
	ds->opts->ignore_transformations = true;

	heif_context_get_primary_image_ID(ds->ctx, &ds->primary_id);
	return get_image(infile, wuconf, 0);
}

enum wu_error heif_dec(struct image_file *infile,
const struct wu_conf *wuconf) {
	struct heif_state *ds = calloc(sizeof(*ds), 1);
	if (ds) {
		infile->dec_state = ds;
		infile->events = ev_subcycle;
		return dec_wrap(infile, wuconf, ds);
	}
	return wu_alloc_error;
}

enum wu_error avif_dec(struct image_file *infile,
const struct wu_conf *wuconf) {
	return heif_dec(infile, wuconf);
}

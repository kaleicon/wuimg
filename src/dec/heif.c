#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include <libheif/heif.h>

#include "../wudefs.h"
#include "../common.h"
#include "../metadata.h"

struct reader_userdata {
	FILE *ifp;
	long size;
};

static int64_t reader_get_position(void *userdata) {
	const struct reader_userdata *ud = userdata;
	return (int64_t)ftell(ud->ifp);
}

static int reader_read(void *restrict data, const size_t size,
void *restrict userdata) {
	const struct reader_userdata *ud = userdata;
	return (int)(size - fread(data, 1, size, ud->ifp));
}

static int reader_seek(const int64_t position, void *userdata) {
	const struct reader_userdata *ud = userdata;
	return fseek(ud->ifp, position, SEEK_SET);
}

static enum heif_reader_grow_status reader_wait_for_file_size(
const int64_t target_size, void *userdata) {
	const struct reader_userdata *ud = userdata;
	if (target_size > ud->size) {
		return heif_reader_grow_status_size_beyond_eof;
	}
	return heif_reader_grow_status_size_reached;
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

enum wu_error heif_dec(struct image_file *infile,
const struct wu_conf *wuconf) {
	const struct heif_reader reader = {
		.reader_api_version = 1,
		.get_position = reader_get_position,
		.read = reader_read,
		.seek = reader_seek,
		.wait_for_file_size = reader_wait_for_file_size,
	};
	struct reader_userdata userdata = {
		.ifp = infile->ifp,
		.size = file_get_remaining(infile->ifp),
	};
	struct heif_context *ctx = heif_context_alloc();
	struct heif_error herr = heif_context_read_from_reader(ctx, &reader,
		&userdata, NULL);
	if (herr.code != heif_error_Ok) {
		image_file_error_append(infile, herr.message);
		heif_context_free(ctx);
		return wu_open_error;
	}

	struct heif_decoding_options *heif_opts = heif_decoding_options_alloc();
	heif_opts->ignore_transformations = 1;

	const int nb_ids = heif_context_get_number_of_top_level_images(ctx);
	heif_item_id *hids = malloc(sizeof(*hids) * (size_t)nb_ids);
	if (!hids) {
		heif_context_free(ctx);
		return wu_alloc_error;
	}
	heif_context_get_list_of_top_level_image_IDs(ctx, hids, nb_ids);

	heif_item_id primary_id;
	heif_context_get_primary_image_ID(ctx, &primary_id);
	struct raw_img *img = alloc_sub_images(infile, (size_t)nb_ids);
	size_t decoded = 0;
	for (size_t i = 0; i < infile->nr; ++i) {
		struct heif_image_handle *handle;
		herr = heif_context_get_image_handle(ctx, hids[i], &handle);
		if (herr.code != heif_error_Ok) {
			printf("Couldn't get handle for sub-image %zu: %s\n",
				i, herr.message);
			continue;
		}
		handle_metadata(handle, &infile->metadata);

		const int width = heif_image_handle_get_ispe_width(handle);
		const int height = heif_image_handle_get_ispe_height(handle);
		if ((unsigned int)imax(width, height) > wuconf->max_img_size) {
			printf("Sub-image %zu exceeds the image size limit. "
				"Skipping.", i);
			continue;
		}

		unsigned char channels;
		const enum heif_chroma chroma = calc_chroma(handle, &channels);

		struct heif_image *himg;
		herr = heif_decode_image(handle, &himg, heif_colorspace_RGB,
			chroma, heif_opts);
		heif_image_handle_release(handle);
		if (herr.code != heif_error_Ok) {
			printf("Error decoding sub-image %zu: %s\n", i,
				herr.message);
			continue;
		}

		if (hids[i] != primary_id) {
			img[decoded].id = id_template("id", hids[i]);
		}

		const enum heif_channel interleaved = heif_channel_interleaved;
		int bytes_per_line;
		img[decoded].data = heif_image_get_plane(himg, interleaved,
			&bytes_per_line);
		img[decoded].w = (size_t)heif_image_get_width(himg, interleaved);
		img[decoded].h = (size_t)heif_image_get_height(himg, interleaved);
		img[decoded].channels = channels;
		img[decoded].bitdepth = (unsigned char)(
			heif_image_get_bits_per_pixel(himg, interleaved)
			/ img[decoded].channels);
		img[decoded].alignment = 16;

		const int bit_range = heif_image_get_bits_per_pixel_range(himg,
			interleaved);
		if (bit_range > 8 && bit_range < 16) {
			scale_depth(img + decoded, bit_range, bytes_per_line);
		}
		++decoded;
	}
	heif_decoding_options_free(heif_opts);
	heif_context_free(ctx);
	free(hids);
	return image_file_total_decoded(infile, decoded);
}

enum wu_error avif_dec(struct image_file *infile,
const struct wu_conf *wuconf) {
	return heif_dec(infile, wuconf);
}

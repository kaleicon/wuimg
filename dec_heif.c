#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libheif/heif.h>

#include "wudefs.h"
#include "common.h"

enum wu_error heif_dec(struct image_file *infile,
const struct wu_conf *wuconf) {
	size_t size;
	unsigned char *data = read_file_to_mem(infile->ifp, &size);
	if (!data) {
		return wu_alloc_error;
	}

	struct heif_context *ctx = heif_context_alloc();
	struct heif_error herr;
	herr = heif_context_read_from_memory_without_copy(ctx, data, size, NULL);
	if (herr.code != heif_error_Ok) {
		infile->err_msg = strdup(herr.message);
		heif_context_free(ctx);
		free(data);
		return wu_open_error;
	}

	struct heif_decoding_options *heif_opts = heif_decoding_options_alloc();
	heif_opts->ignore_transformations = 1;

	const size_t ids = (size_t)heif_context_get_number_of_top_level_images(ctx);
	heif_item_id *hids = malloc((size_t)ids * sizeof(heif_item_id));
	heif_context_get_list_of_top_level_image_IDs(ctx, hids, (int)ids);

	heif_item_id primary_id;
	heif_context_get_primary_image_ID(ctx, &primary_id);
	struct raw_img *img = alloc_sub_images(infile, ids);
	size_t decoded = 0;
	for (size_t i = 0; i < infile->nr; ++i) {
		struct heif_image_handle *handle;
		herr = heif_context_get_image_handle(ctx, hids[i], &handle);
		if (herr.code != heif_error_Ok) {
			printf("Couldn't get handle for sub-image %zu: %s\n",
				i, herr.message);
			continue;
		}
		const int width = heif_image_handle_get_ispe_width(handle);
		const int height = heif_image_handle_get_ispe_height(handle);
		if ((unsigned int)imax(width, height) > wuconf->max_img_size) {
			printf("Sub-image %zu exceeds the image size limit. "
				"Skipping.", i);
		}

		img[decoded].w = (size_t)width;
		img[decoded].h = (size_t)height;
		img[decoded].bitdepth = 8;
		enum heif_chroma chroma;
		if (heif_image_handle_has_alpha_channel(handle)) {
			chroma = heif_chroma_interleaved_RGBA;
			img[decoded].channels = 4;
		} else {
			chroma = heif_chroma_interleaved_RGB;
			img[decoded].channels = 3;
		}
		if (hids[i] != primary_id) {
			img[decoded].id = id_template("id", hids[i]);
		}

		struct heif_image *himg;
		herr = heif_decode_image(handle, &himg, heif_colorspace_RGB,
			chroma, heif_opts);
		heif_image_handle_release(handle);
		if (herr.code != heif_error_Ok) {
			printf("Error decoding sub-image %zu: %s\n", i,
				herr.message);
			continue;
		}

		const enum heif_channel interleaved = heif_channel_interleaved;
		img[decoded].data = heif_image_get_plane(himg, interleaved, NULL);
		++decoded;
	}
	heif_decoding_options_free(heif_opts);
	heif_context_free(ctx);
	free(hids);
	free(data);

	int return_code = wu_ok;
	if (decoded == 0) {
		return_code = wu_decoding_error;
	} else if (decoded < infile->nr) {
		realloc_sub_images(infile, decoded);
	}

	return return_code;
}

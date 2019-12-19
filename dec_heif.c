#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libheif/heif.h>

#include "wudefs.h"
#include "common.h"

enum wu_error_type heif_dec(struct image_file *infile) {
	struct heif_context *ctx = heif_context_alloc();
	struct heif_error herr;

	herr = heif_context_read_from_file(ctx, infile->name, NULL);
	if (herr.code != heif_error_Ok) {
		infile->err_msg = strdup(herr.message);
		heif_context_free(ctx);
		return wu_open_error;
	}

	struct heif_decoding_options *heif_opts = heif_decoding_options_alloc();
	heif_opts->ignore_transformations = 1;

	const int tli = heif_context_get_number_of_top_level_images(ctx);
	heif_item_id *hids = malloc((size_t)tli * sizeof(heif_item_id));
	const size_t ids = (size_t)heif_context_get_list_of_top_level_image_IDs(
		ctx, hids, tli);

	struct raw_img *img = alloc_sub_images(infile, ids);
	size_t decoded = 0;
	for (size_t i = 0; i < infile->nr; ++i) {
		struct heif_error herr;
		struct heif_image_handle *handle;
		herr = heif_context_get_image_handle(ctx, hids[i], &handle);
		if (herr.code != heif_error_Ok) {
			printf("Error obtaining handle for image %zu in %s: "
				"%s\n", i, infile->name, herr.message);
			continue;
		}

		if (!heif_image_handle_is_primary_image(handle)) {
			img[decoded].id = id_template("id", hids[i]);
		}

		enum heif_chroma chroma;
		if (heif_image_handle_has_alpha_channel(handle)) {
			chroma = heif_chroma_interleaved_RGBA;
			img[decoded].channels = 4;
		} else {
			chroma = heif_chroma_interleaved_RGB;
			img[decoded].channels = 3;
		}

		struct heif_image *himg;
		herr = heif_decode_image(handle, &himg, heif_colorspace_RGB,
			chroma, heif_opts);
		heif_image_handle_release(handle);
		if (herr.code != heif_error_Ok) {
			printf("Error decoding image %zu in %s: %s\n", i,
				infile->name, herr.message);
			continue;
		}

		const enum heif_channel interleaved = heif_channel_interleaved;
		img[decoded].w = (unsigned int)heif_image_get_width(himg, interleaved);
		img[decoded].h = (unsigned int)heif_image_get_height(himg, interleaved);
		img[decoded].data = heif_image_get_plane(himg, interleaved, NULL);
		img[decoded].bitdepth = 8;
		++decoded;
	}
	heif_decoding_options_free(heif_opts);
	free(hids);
	heif_context_free(ctx);

	int return_code = wu_ok;
	if (decoded == 0) {
		return_code = wu_decoding_error;
	} else if (decoded < infile->nr) {
		fit_sub_images(infile, decoded);
	}

	return return_code;
}

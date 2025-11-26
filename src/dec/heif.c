// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2020 kaleido
#include <stdlib.h>
#include <string.h>

#include <libheif/heif.h>

#include "dec_enable.def"
#include "misc/metadata.h"
#include "wudefs.h"

struct heif_state {
	struct heif_init_params params;
	struct heif_context *ctx;
	struct heif_decoding_options *opts;
	heif_item_id *hids;
	struct heif_image **himgs;
};

static void end_heif(struct image_file *infile) {
	struct heif_state *ds = infile->dec_state;
	if (ds->himgs) {
		for (size_t i = 0; i < infile->nr; ++i) {
			if (ds->himgs[i]) {
				heif_image_release(ds->himgs[i]);
			}
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
	heif_deinit();
}

static void read_block(const struct heif_image_handle* handle,
struct wuimg *img, const heif_item_id id, unsigned char *buf,
const size_t len) {
	const struct heif_error herr = heif_image_handle_get_metadata(handle,
		id, buf);
	if (herr.code == heif_error_Ok) {
		struct wutree *meta = wuimg_get_metadata(img);
		if (meta) {
			const char *type = heif_image_handle_get_metadata_type(
				handle, id);
			if (!strcmp(type, "Exif") && len > 10) {
				metadata_parse(metadata_exif, buf + 10,
					len - 10, meta);
			} else if (!strcmp(type, "mime")) {
				metadata_parse(metadata_xmp, buf, len, meta);
			} else {
				tree_add_leaf_utf8(meta,
					"Found metadata block", type);
			}
		}
	}
}

static void read_heif_metadata(const struct heif_image_handle* handle,
struct wuimg *img) {
	const int blocks = heif_image_handle_get_number_of_metadata_blocks(
		handle, NULL);
	heif_item_id *ids = malloc(sizeof(*ids) * (size_t)blocks);
	if (ids) {
		heif_image_handle_get_list_of_metadata_block_IDs(handle, NULL,
			ids, blocks);
		for (int i = 0; i < blocks; ++i) {
			const size_t len = heif_image_handle_get_metadata_size(
				handle, ids[i]);
			unsigned char *buf = malloc(len);
			if (buf) {
				read_block(handle, img, ids[i], buf, len);
				free(buf);
			}
		}
		free(ids);
	}
}

static void get_heif_color_profile(struct color_space *cs,
const struct heif_image *himg) {
	struct heif_color_profile_nclx *nclx;
	struct heif_error herr = heif_image_get_nclx_color_profile(himg, &nclx);
	if (herr.code == heif_error_Ok) {
		cs->primaries = (enum cicp_primaries)nclx->color_primaries;
		cs->transfer = (enum cicp_transfer)nclx->transfer_characteristics;
		cs->matrix = (enum cicp_matrix)nclx->matrix_coefficients;
		cs->limited = !nclx->full_range_flag;
		heif_nclx_color_profile_free(nclx);
	} else {
		const size_t len = heif_image_get_raw_color_profile_size(himg);
		if (len) {
			void *data = malloc(len);
			if (data) {
				herr = heif_image_get_raw_color_profile(himg,
					data);
				if (herr.code == heif_error_Ok) {
					color_space_set_icc_owned(cs, data, len);
				} else {
					free(data);
				}
			}
		}
	}
}

static struct wu_st deduce_alignment(struct wuimg *img, const int bpl[static 4],
const size_t scanline[static 4], const uint8_t comps) {
	align_t max = 0;
	for (uint8_t z = 0; z < comps; ++z) {
		const align_t align = strip_alignment((size_t)bpl[z],
			scanline[z], img->bitdepth);
		if (align < 0) {
			return wuerr(wu_invalid_header,
				"non power-of-two alignment");
		} else if (align > max) {
			max = align;
		}
	}
	for (uint8_t z = 0; z < comps; ++z) {
		const size_t expect = strip_length(scanline[z], img->bitdepth,
			max);
		if (expect != (size_t)bpl[z]) {
			return wuerr(wu_unsupported_feature,
				"strip length mismatch");
		}
	}
	img->align_sh = max;
	return WU_OK;
}

static struct wu_st get_planar_image(struct wuimg *img,
struct heif_image *himg, const enum heif_chroma chroma,
const enum heif_colorspace cs, const bool alpha, int bpl[static 4],
size_t scanlines[static 4]) {
	const enum heif_channel mono_chs[] = {heif_channel_Y, heif_channel_Alpha};
	const enum heif_channel color_chs[] = {
		(cs == heif_colorspace_RGB) ? heif_channel_R : heif_channel_Y,
		(cs == heif_colorspace_RGB) ? heif_channel_G : heif_channel_Cb,
		(cs == heif_colorspace_RGB) ? heif_channel_B : heif_channel_Cr,
		heif_channel_Alpha,
	};
	const enum heif_channel *chs = (chroma == heif_chroma_monochrome)
		? mono_chs : color_chs;

	const int depth = heif_image_get_bits_per_pixel_range(himg, chs[0]);
	if (depth == -1) {
		return WUERR_HERE(wu_invalid_params);
	}

	img->channels = (uint8_t)((chroma == heif_chroma_monochrome ? 1 : 3)
		+ alpha);
	img->bitdepth = (depth > 8) ? 16 : 8;
	img->bitrange = (uint8_t)depth;
	for (uint8_t c = 1; c < img->channels; ++c) {
		const int d = heif_image_get_bits_per_pixel_range(himg, chs[c]);
		if (d != -1 && d != depth) {
			return wuerr(wu_unsupported_feature, "planes with"
				" different pixel ranges unsupported");
		}
	}

	struct image_planes *planes = wuimg_plane_init(img);
	if (!planes) {
		return WUERR_HERE(wu_alloc_error);
	}

	switch (chroma) {
	case heif_chroma_420: wuimg_plane_subsamp(img, 2, 2); break;
	case heif_chroma_422: wuimg_plane_subsamp(img, 2, 1); break;
	default: break;
	}
	wuimg_plane_resolve(img);

	const enum wu_error st = wuimg_verify(img);
	if (st == wu_ok) {
		for (uint8_t c = 0; c < img->channels; ++c) {
			struct plane_info *p = planes->p + c;
			p->ptr = heif_image_get_plane(himg, chs[c],
				bpl + c);
			scanlines[c] = (size_t)heif_image_get_width(
				himg, chs[c]);
		}
	}
	return WUERR_CHECK(st);
}

static struct wu_st get_interleaved_image(struct wuimg *img,
struct heif_image *himg, const bool alpha, int *bpl, size_t *scanline) {
	const enum heif_channel hch = heif_channel_interleaved;
	const int depth = heif_image_get_bits_per_pixel_range(himg, hch);

	img->channels = 3 + alpha;
	img->bitdepth = (depth > 8) ? 16 : 8;
	img->bitrange = (uint8_t)depth;
	const enum wu_error st = wuimg_verify(img);
	if (st == wu_ok) {
		img->data = heif_image_get_plane(himg, hch, bpl);
	}
	*scanline = img->w * img->channels;
	return WUERR_CHECK(st);
}

static struct wu_st event_heif(struct image_file *infile,
const struct wu_conf *conf, struct wu_state *state, const enum image_event ev) {
	if (ev != ev_subcycle) {
		return WU_NO_CHANGE;
	}
	const size_t i = (size_t)state->idx;

	struct heif_state *ds = infile->dec_state;
	struct wuimg *img = infile->sub_img + i;
	if (img->borrowed) {
		return WU_NO_CHANGE;
	}

	struct heif_image_handle *handle;
	struct heif_error herr = heif_context_get_image_handle(ds->ctx,
		ds->hids[i], &handle);
	if (herr.code != heif_error_Ok) {
		image_file_strerror_append(infile, herr.message);
		return WUERR_HERE(wu_decoding_error);
	}

	const bool has_alpha = heif_image_handle_has_alpha_channel(handle);
	if (has_alpha) {
		img->alpha = heif_image_handle_is_premultiplied_alpha(handle)
			? alpha_associated : alpha_unassociated;
	}

	read_heif_metadata(handle, img);

	enum heif_colorspace colorspace = heif_colorspace_undefined;
	enum heif_chroma chroma = heif_chroma_undefined;
	herr = heif_decode_image(handle, ds->himgs + i, colorspace, chroma,
		ds->opts);
	heif_image_handle_release(handle);
	if (herr.code != heif_error_Ok) {
		image_file_strerror_append(infile, herr.message);
		return WUERR_HERE(wu_decoding_error);
	}

	struct heif_image *himg = ds->himgs[i];
	img->w = (size_t)heif_image_get_primary_width(himg);
	img->h = (size_t)heif_image_get_primary_height(himg);
	if (wuimg_exceeds_limit(img, conf)) {
		return WUERR_HERE(wu_exceeds_size_limit);
	}
	img->borrowed = true;

	get_heif_color_profile(&img->cs, himg);
	colorspace = heif_image_get_colorspace(himg);
	chroma = heif_image_get_chroma_format(himg);
	struct wu_st st = WUERR_HERE(wu_invalid_params);
	uint8_t comps = 0;
	int bytes_per_line[4];
	size_t scanlines[4];
	switch (chroma) {
	case heif_chroma_undefined:
		return st;
	case heif_chroma_monochrome:
	case heif_chroma_420:
	case heif_chroma_422:
	case heif_chroma_444:
		st = get_planar_image(img, himg, chroma, colorspace, has_alpha,
			bytes_per_line, scanlines);
		comps = img->channels;
		break;
	default:
		st = get_interleaved_image(img, himg, has_alpha,
			bytes_per_line, scanlines);
		comps = 1;
		break;
	}
	if (wu_isok(st)) {
		st = deduce_alignment(img, bytes_per_line, scanlines, comps);
		if (wu_isok(st) && img->mode == image_mode_planar) {
			wuimg_plane_resolve(img);
		}
	}
	return st;
}

static struct wu_st init_heif(struct image_file *infile,
const struct wu_conf *_c) {
	(void)_c;
	struct heif_state *ds = infile->dec_state;
	infile->dec_state = ds;
	struct heif_error herr = heif_init(&ds->params);
	if (herr.code != heif_error_Ok) {
		image_file_strerror_append(infile, herr.message);
		return WUERR_HERE(wu_open_error);
	}

	ds->ctx = heif_context_alloc();
	herr = heif_context_read_from_memory_without_copy(ds->ctx,
		infile->map.ptr, infile->map.len, NULL);
	if (herr.code != heif_error_Ok) {
		image_file_strerror_append(infile, herr.message);
		return WUERR_HERE(wu_open_error);
	}

	struct wuimg *img = alloc_sub_images(infile,
		(size_t)heif_context_get_number_of_top_level_images(ds->ctx));
	ds->hids = malloc(infile->nr * sizeof(*ds->hids));
	ds->himgs = calloc(infile->nr, sizeof(*ds->himgs));
	if (!img || !ds->hids || !ds->himgs) {
		return WUERR_HERE(wu_alloc_error);
	}
	heif_context_get_list_of_top_level_image_IDs(ds->ctx, ds->hids,
		(int)infile->nr);

	if (false) {
		ds->opts = heif_decoding_options_alloc();
		ds->opts->ignore_transformations = true;
	}
	return WU_OK;
}

const struct image_fn heif_fn = {
	.mmap = true,
	.state_size = sizeof(struct heif_state),
	.init = init_heif,
	.event = event_heif,
	.end = end_heif,
};
#ifndef WU_ENABLE_AVIF
const struct image_fn avif_fn = heif_fn;
#endif

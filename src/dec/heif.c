#include <libheif/heif.h>

#include "dec_enable.def"
#include "common/file.h"
#include "metadata.h"
#include "wudefs.h"

struct heif_state {
	struct map_info mm;
	struct heif_context *ctx;
	struct heif_decoding_options *opts;
	heif_item_id *hids;
	struct heif_image **himgs;
	size_t decoded;
	heif_item_id primary_id;
};

static void clean_heif_state(struct image_file *infile) {
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
	file_unmap(&ds->mm);
}

static void read_block(const struct heif_image_handle* handle,
struct raw_img *img, const heif_item_id id, unsigned char *buf,
const size_t len) {
	const struct heif_error herr = heif_image_handle_get_metadata(handle,
		id, buf);
	if (herr.code == heif_error_Ok) {
		if (!img->metadata) {
			img->metadata = tree_plant(NULL);
		}
		if (img->metadata) {
			const char *type = heif_image_handle_get_metadata_type(
				handle, id);
			if (!strcmp(type, "Exif") && len > 10) {
				standard_metadata(exif_metadata, buf + 10,
					len - 10, img->metadata);
			} else if (!strcmp(type, "mime")) {
				standard_metadata(xmp_metadata, buf, len,
					img->metadata);
			} else {
				tree_add_leaf_utf8(img->metadata,
					"Found metadata block", type);
			}
		}
	}
}

static void read_metadata(const struct heif_image_handle* handle,
struct raw_img *img) {
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

static void set_colorspace(struct color_space *cs,
struct heif_color_profile_nclx *nclx) {
	color_space_set_primaries(cs,
		nclx->color_primary_white_x, nclx->color_primary_white_y,
		nclx->color_primary_red_x, nclx->color_primary_red_y,
		nclx->color_primary_green_x, nclx->color_primary_green_y,
		nclx->color_primary_blue_x, nclx->color_primary_blue_y);
//	cs->primaries = nclx->color_primaries;
	cs->transfer = (enum cicp_transfer)nclx->transfer_characteristics;
	cs->matrix = (enum cicp_matrix)nclx->matrix_coefficients;
	cs->limited = !nclx->full_range_flag;
}

static void get_color_profile(struct color_space *cs,
const struct heif_image *himg) {
	struct heif_error herr;
	switch (heif_image_get_color_profile_type(himg)) {
	case heif_color_profile_type_not_present:
		break;
	case heif_color_profile_type_prof: // ICC
	case heif_color_profile_type_rICC: // restricted ICC
		;const size_t len = heif_image_get_raw_color_profile_size(himg);
		if (len) {
			void *data = malloc(len);
			if (data) {
				herr = heif_image_get_raw_color_profile(himg,
					data);
				if (herr.code == heif_error_Ok) {
					color_space_set_icc_owned(cs, data, len);
					return;
				}
				free(data);
			}
		}
		// fallthrough
	case heif_color_profile_type_nclx:
		;struct heif_color_profile_nclx *nclx;
		herr = heif_image_get_nclx_color_profile(himg, &nclx);
		if (herr.code == heif_error_Ok) {
			set_colorspace(cs, nclx);
			heif_nclx_color_profile_free(nclx);
		}
		break;
	}
}

static enum wu_error get_planar_image(struct raw_img *img,
struct heif_image *himg, const enum heif_chroma chroma, const bool alpha,
int *bpl) {
	const enum heif_colorspace cs = heif_image_get_colorspace(himg);
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
		return wu_invalid_params;
	}

	img->borrowed = true;
	img->channels = (uint8_t)((chroma == heif_chroma_monochrome ? 1 : 3)
		+ alpha);
	img->bitdepth = (depth > 8) ? 16 : 8;
	img->used_bits = (uint8_t)depth;
	for (uint8_t c = 1; c < img->channels; ++c) {
		const int d = heif_image_get_bits_per_pixel_range(himg, chs[c]);
		if (d != -1 && d != depth) {
			return wu_unsupported_feature;
		}
	}

	struct image_planes *planes = raw_img_plane_init(img);
	if (!planes) {
		return wu_alloc_error;
	}

	uint8_t h = 1;
	uint8_t v = 1;
	switch (chroma) {
	case heif_chroma_420: h = 2; v = 2; break;
	case heif_chroma_422: h = 2; v = 1; break;
	default: break;
	}

	raw_img_plane_subsamp(img, h, v);
	raw_img_plane_resolve(img);

	const enum wu_error st = raw_img_verify(img);
	if (st == wu_ok) {
		for (uint8_t c = 0; c < img->channels; ++c) {
			int bytes_per_line;
			struct plane_info *p = planes->p + c;
			p->ptr = heif_image_get_plane(himg, chs[c],
				&bytes_per_line);
			if (c == 0) {
				*bpl = bytes_per_line;
			}
		}
	}
	return st;
}

static enum wu_error get_interleaved_image(struct raw_img *img,
struct heif_image *himg, const bool alpha, int *bpl) {
	const enum heif_channel hch = heif_channel_interleaved;
	const int depth = heif_image_get_bits_per_pixel_range(himg, hch);

	img->channels = 3 + alpha;
	img->bitdepth = (depth > 8) ? 16 : 8;
	img->used_bits = (uint8_t)depth;
	const enum wu_error st = raw_img_verify(img);
	if (st == wu_ok) {
		img->data = heif_image_get_plane(himg, hch, bpl);
	}
	return st;
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

	img->alpha = heif_image_handle_is_premultiplied_alpha(handle)
		? alpha_associated : alpha_unassociated;

	read_metadata(handle, img);

	const bool has_alpha = heif_image_handle_has_alpha_channel(handle);
	herr = heif_decode_image(handle, ds->himgs + i,
		heif_colorspace_undefined, heif_chroma_undefined, ds->opts);
	heif_image_handle_release(handle);
	if (herr.code != heif_error_Ok) {
		image_file_error_append(infile, herr.message);
		return wu_decoding_error;
	}

	struct heif_image *himg = ds->himgs[i];
	img->w = (size_t)heif_image_get_primary_width(himg);
	img->h = (size_t)heif_image_get_primary_height(himg);
	if (raw_img_exceeds_limit(img, wuconf)) {
		return wu_exceeds_size_limit;
	}

	get_color_profile(&img->cs, himg);

	const enum heif_chroma chroma = heif_image_get_chroma_format(himg);
	enum wu_error st = wu_invalid_params;
	int bytes_per_line;
	size_t scanline;
	switch (chroma) {
	case heif_chroma_undefined:
		return wu_invalid_params;
	case heif_chroma_monochrome:
	case heif_chroma_420:
	case heif_chroma_422:
	case heif_chroma_444:
		st = get_planar_image(img, himg, chroma, has_alpha,
			&bytes_per_line);
		scanline = img->w;
		break;
	default:
		st = get_interleaved_image(img, himg, has_alpha, &bytes_per_line);
		scanline = img->w * img->channels;
		break;
	}
	if (st == wu_ok) {
		img->align_sh = strip_alignment((size_t)bytes_per_line,
			scanline, img->bitdepth);
		if (img->align_sh < 0) {
			return wu_invalid_header;
		}
		if (img->mode == image_mode_planar) {
			raw_img_plane_resolve(img);
		}
		++ds->decoded;
		if (ds->decoded == infile->nr) {
			infile->events = 0;
		}
	}
	return st;
}

enum wu_error heif_callback(struct image_file *infile,
const struct wu_conf *wuconf, struct wu_state *state,
const enum image_event ev) {
	if (ev == ev_subcycle) {
		const size_t i = (size_t)state->idx;
		if (!infile->sub_img[i].data) {
			return get_image(infile, wuconf, i);
		}
	} else {
		clean_heif_state(infile);
	}
	return wu_no_change;
}

#ifndef WU_ENABLE_AVIF
enum wu_error avif_callback(struct image_file *infile,
const struct wu_conf *wuconf, struct wu_state *state,
const enum image_event ev) {
	return heif_callback(infile, wuconf, state, ev);
}
#endif

enum wu_error heif_dec(struct image_file *infile,
const struct wu_conf *wuconf) {
	struct heif_state *ds = calloc(sizeof(*ds), 1);
	if (!ds) {
		return wu_alloc_error;
	}

	infile->dec_state = ds;
	infile->events = ev_subcycle;
	if (!file_map(&ds->mm, infile->ifp)) {
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

	/* libheif doesn't expose orientation information, so we must let the
	 * decoder handle it. */
	ds->opts = NULL; //heif_decoding_options_alloc();
	//ds->opts->ignore_transformations = true;

	heif_context_get_primary_image_ID(ds->ctx, &ds->primary_id);
	return get_image(infile, wuconf, 0);
}

#ifndef WU_ENABLE_AVIF
enum wu_error avif_dec(struct image_file *infile,
const struct wu_conf *wuconf) {
	return heif_dec(infile, wuconf);
}
#endif

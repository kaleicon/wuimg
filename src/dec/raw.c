#include <libraw/libraw.h>

#include "../wudefs.h"
#include "../common.h"
#include "../metadata.h"

#include "dec_enable.def"
#ifdef WU_ENABLE_JPEG
#include "jpeg.h"
#endif
enum raw_thumbnail {
	raw_thumb_none,
	raw_thumb_bitmap,
	raw_thumb_jpeg,
};

struct raw_image_info {
	size_t count;
	libraw_processed_image_t **proc;
};

struct raw_state {
	struct mmap_file map;
	libraw_data_t *data;
	struct raw_image_info raw;
	enum raw_thumbnail thumb_type;
	struct image_file jpeg;
};

static enum wu_error raw_error_to_wu(struct image_file *infile, const int err) {
	infile->err_msg = strdup(libraw_strerror(err));
	switch (err) {
	case LIBRAW_SUCCESS: return wu_ok;
	case LIBRAW_UNSPECIFIED_ERROR: return wu_unknown_error;
	case LIBRAW_FILE_UNSUPPORTED: return wu_unknown_file_type;
	case LIBRAW_REQUEST_FOR_NONEXISTENT_IMAGE: return wu_decoding_error;
	case LIBRAW_OUT_OF_ORDER_CALL: return wu_invalid_params;
	case LIBRAW_NOT_IMPLEMENTED: return wu_unsupported_feature;
	case LIBRAW_UNSUFFICIENT_MEMORY: return wu_alloc_error;
	case LIBRAW_BAD_CROP: return wu_decoding_error;
	case LIBRAW_TOO_BIG: return wu_alloc_error;
	}
	return wu_unknown_error;
}

static void raw_state_free(struct image_file *infile) {
	struct raw_state *rs = infile->dec_state;
	struct raw_img *img = infile->sub_img;
	size_t i = 0;
	while (i < rs->raw.count) {
		libraw_dcraw_clear_mem(rs->raw.proc[i]);
		img[i].data = NULL;
		++i;
	}
	free(rs->raw.proc);
	if (rs->thumb_type == raw_thumb_bitmap) {
		img[i].data = NULL;
	} else if (rs->jpeg.events) {
		jpeg_callback(&rs->jpeg, NULL, NULL, ev_end);
		rs->jpeg.nr = 0;
		rs->jpeg.sub_img = NULL;
		image_file_free(&rs->jpeg);
	}

	libraw_close(rs->data);
	munmap_stream(rs->map);
	free(rs);
	infile->dec_state = NULL;
	infile->events = 0;
}

static enum wu_error append_jpeg(struct image_file *infile,
const struct wu_conf *wuconf, struct raw_state *rs) {
	struct image_file *jpeg = &rs->jpeg;
	enum wu_error status = jpeg_dec(jpeg, wuconf);
	if (status == wu_ok) {
		struct raw_img *img = infile->sub_img;
		if (jpeg->nr > 1) {
			img = realloc_sub_images(infile, infile->nr + jpeg->nr - 1);
			if (!img) {
				return wu_alloc_error;
			}
		}

		struct raw_img *jimg = img + rs->raw.count;
		memcpy(jimg, jpeg->sub_img, jpeg->nr * sizeof(*jpeg->sub_img));
		free(jpeg->sub_img);
		jpeg->sub_img = jimg;
		infile->events = jpeg->events;
	} else {
		image_file_free(jpeg);
	}
	return status;
}

static enum wu_error raw_decode(struct image_file *infile,
const struct wu_conf *wuconf, const size_t i) {
	struct raw_state *rs = infile->dec_state;
	struct raw_img *img = infile->sub_img + i;

	if (i < rs->raw.count) {
		libraw_data_t *data = rs->data;
		data->params.shot_select = (unsigned int)i;

		int err = libraw_dcraw_process(data);
		if (err != LIBRAW_SUCCESS) {
			infile->err_msg = strdup(libraw_strerror(err));
			return wu_decoding_error;
		}

		libraw_processed_image_t *proc = libraw_dcraw_make_mem_image(
			data, NULL);
		if (!proc) {
			return wu_alloc_error;
		}
		rs->raw.proc[i] = proc;
		if (umax(proc->width, proc->height) > wuconf->max_img_size) {
			return wu_exceeds_size_limit;
		}

		img->data = proc->data;
		img->w = proc->width;
		img->h = proc->height;
		img->channels = (unsigned char)proc->colors;
		img->bitdepth = (unsigned char)proc->bits;
	} else {
		const libraw_thumbnail_t *thumb = &rs->data->thumbnail;
		if (thumb->tformat == LIBRAW_THUMBNAIL_JPEG) {
			struct image_file *jpeg = &rs->jpeg;
			enum wu_error status = wu_open_error;
			jpeg->ifp = fmemopen(thumb->thumb, thumb->tlength, "rb");
			if (jpeg->ifp) {
				status = append_jpeg(infile, wuconf, rs);
			}

			if (status != wu_ok) {
				infile->err_msg = strdup("Failed to decode "
					"JPEG thumbnail");
				if (infile->nr <= 1) {
					return status;
				}
				realloc_sub_images(infile, infile->nr - 1);
			}
		}
	}
	return wu_ok;
}

enum wu_error raw_callback(struct image_file *infile,
const struct wu_conf *wuconf, struct wu_state *state,
const enum image_event ev) {
	struct raw_state *rs = infile->dec_state;
	enum wu_error status = wu_no_change;
	if (ev) {
		struct raw_img *img = infile->sub_img;
		const int raws = (int)rs->raw.count;
		if (state->idx >= raws && rs->jpeg.ifp) {
			state->idx -= raws;
			status = jpeg_callback(&rs->jpeg, wuconf, state, ev);
			state->idx += raws;
		} else if (!img[state->idx].data) {
			status = raw_decode(infile, wuconf, (size_t)state->idx);
		}
	}

	if (!ev || status > wu_ok) {
		raw_state_free(infile);
	}
	return status;
}

static unsigned char convert_rotate(const int flip) {
	switch (flip) {
	case 3: return 2;
	case 5: return 1;
	case 6: return 3;
	default: return 0;
	}
}

static bool big_enough_thumb(const libraw_data_t *data) {
	const libraw_thumbnail_t *thumb = &data->thumbnail;
	const unsigned int img_dims = umin(data->sizes.width, data->sizes.height);
	const unsigned int thumb_dims = umax(thumb->twidth, thumb->theight);
	return thumb_dims >= img_dims / 2;
}

static enum raw_thumbnail unpack_thumb(const struct wu_conf *wuconf,
libraw_data_t *data) {
	const libraw_thumbnail_t *thumb = &data->thumbnail;
	const unsigned int thumb_dims = umax(thumb->twidth, thumb->theight);
	if (thumb_dims <= wuconf->max_img_size) {
		if (libraw_unpack_thumb(data) == LIBRAW_SUCCESS) {
			switch (data->thumbnail.tformat) {
			case LIBRAW_THUMBNAIL_JPEG:
#ifdef WU_ENABLE_JPEG
				return raw_thumb_jpeg;
#else
				break;
#endif
			case LIBRAW_THUMBNAIL_BITMAP:
			case LIBRAW_THUMBNAIL_BITMAP16:
				return raw_thumb_bitmap;
			default:
				break;
			}
		}
	}
	return raw_thumb_none;
}

static void measure_and_add(struct wu_tree *tree, const char *restrict name,
const char *restrict field, const size_t field_len) {
	const size_t len = strnlen(field, field_len);
	if (len) {
		tree_sprout_unsafe_leaf(tree, name, field, len);
	}
}

static void read_metadata(struct wu_tree *tree, const libraw_data_t *data) {
	const libraw_imgother_t *other = &data->other;
	measure_and_add(tree, "Artist", other->artist, sizeof(other->artist));
	measure_and_add(tree, "Description", other->desc, sizeof(other->desc));
	const struct wu_tree_sap sap[] = {
		{"ISO speed", wu_leaf_double, {.g = other->iso_speed}},
		{"Shutter speed", wu_leaf_double, {.g = other->shutter}},
		{"Aperture", wu_leaf_double, {.g = other->aperture}},
		{"Focal length", wu_leaf_double, {.g = other->focal_len}},
		{"Timestamp", wu_leaf_time, {.time = other->timestamp}},
		{"Shot order", wu_leaf_unsigned, {.u = other->shot_order}},
	};
	tree_bud_leaves(tree, sap, ARRAY_LEN(sap));

	const libraw_iparams_t *idata = &data->idata;
	measure_and_add(tree, "Make", idata->make, sizeof(idata->make));
	measure_and_add(tree, "Model", idata->model, sizeof(idata->model));
	measure_and_add(tree, "Software", idata->software, sizeof(idata->software));
	if (idata->dng_version) {
		const struct wu_leaf leaf = {
			.val.u = idata->dng_version,
			.type = wu_leaf_unsigned
		};
		tree_bud_leaf(tree, "DNG version", leaf);
	}

	standard_metadata(xmp_metadata, idata->xmpdata, idata->xmplen, tree);
}

static enum wu_error raw_setup(struct image_file *infile,
const struct wu_conf *wuconf, struct raw_state *rs) {
	rs->map = mmap_stream(infile->ifp);
	if (rs->map.data == MAP_FAILED) {
		return wu_alloc_error;
	}

	rs->data = libraw_init(0);
	if (!rs->data) {
		return wu_alloc_error;
	}

	int err = libraw_open_buffer(rs->data, rs->map.data, rs->map.len);
	if (err != LIBRAW_SUCCESS) {
		return raw_error_to_wu(infile, err);
	}

	read_metadata(&infile->metadata, rs->data);

	rs->thumb_type = unpack_thumb(wuconf, rs->data);
	size_t nr = (rs->thumb_type != raw_thumb_none);
	if (!wuconf->raw_prefer_thumbnail || !big_enough_thumb(rs->data)) {
		rs->data->params.half_size = wuconf->raw_half_size;
		rs->data->params.output_bps = wuconf->raw_16bit ? 16 : 8;
		rs->data->params.user_flip = 0;
		rs->data->params.user_qual = 0;
		rs->data->params.fbdd_noiserd = 0;
		rs->data->params.use_rawspeed = true;

		err = libraw_unpack(rs->data);
		if (err != LIBRAW_SUCCESS) {
			return raw_error_to_wu(infile, err);
		}

		rs->raw.count = rs->data->idata.raw_count;
		rs->raw.proc = calloc(rs->raw.count, sizeof(*rs->raw.proc));
		if (!rs->raw.proc) {
			return wu_alloc_error;
		}

		nr += rs->raw.count;
	}

	struct raw_img *img = alloc_sub_images(infile, nr);
	if (!img) {
		return wu_alloc_error;
	}

	const unsigned char rotate = convert_rotate(rs->data->sizes.flip);
	for (size_t i = 0; i < infile->nr; ++i) {
		img[i].rotate = rotate;
	}

	if (rs->thumb_type == raw_thumb_bitmap) {
		const libraw_thumbnail_t *thumb = &rs->data->thumbnail;
		img += rs->raw.count;

		img->data = (unsigned char *)thumb->thumb;
		img->w = thumb->twidth;
		img->h = thumb->theight;
		img->channels = 3;
		if (thumb->tformat == LIBRAW_THUMBNAIL_BITMAP16) {
			img->bitdepth = 16;
		} else {
			img->bitdepth = 8;
		}
		img->id = strdup("thumbnail_bitmap");
	}

	infile->events = infile->nr > 1 ? ev_subcycle : 0;
	return raw_decode(infile, wuconf, 0);
}

enum wu_error raw_dec(struct image_file *infile, const struct wu_conf *wuconf) {
	struct raw_state *rs = calloc(1, sizeof(*rs));
	if (!rs) {
		return wu_alloc_error;
	}
	infile->dec_state = rs;

	const enum wu_error status = raw_setup(infile, wuconf, rs);
	if (status != wu_ok) {
		raw_state_free(infile);
	}
	return status;
}

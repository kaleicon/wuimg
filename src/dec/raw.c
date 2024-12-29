// SPDX-License-Identifier: 0BSD
#include <libraw/libraw.h>

#include "wudefs.h"
#include "metadata.h"
#include "misc/common.h"
#include "misc/file.h"
#include "misc/math.h"

#include "dec_enable.def"
#ifdef WU_ENABLE_JPEG
#include "dec.h"
#include "dec_fn.h"
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
	libraw_data_t *data;
	struct raw_image_info raw;
	enum raw_thumbnail thumb_type;
	struct image_context jpeg;
};

static enum wu_error raw_error_to_wu(struct image_file *infile, const int err) {
	image_file_strerror_append(infile, libraw_strerror(err));
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

static void raw_end(struct image_file *infile) {
	struct raw_state *rs = infile->dec_state;
	for (size_t i = 0; i < rs->raw.count; ++i) {
		libraw_dcraw_clear_mem(rs->raw.proc[i]);
	}
	free(rs->raw.proc);
	if (rs->jpeg.file.sub_img) {
		dec_free(&rs->jpeg);
		infile->nr -= rs->jpeg.file.nr;
	}
	libraw_close(rs->data);
}

#ifdef WU_ENABLE_JPEG
static enum wu_error copy_jpeg(struct image_file *infile,
const struct raw_state *rs, const struct image_context *jpeg) {
	struct wuimg *img = infile->sub_img;
	const size_t raw_count = rs->raw.count;
	const struct image_file *injpeg = &jpeg->file;
	if (infile->nr != raw_count + injpeg->nr) {
		img = realloc_sub_images(infile, raw_count + injpeg->nr);
		if (!img) {
			return wu_alloc_error;
		}
	}

	memcpy(img + raw_count, injpeg->sub_img, injpeg->nr * sizeof(*img));
	return wu_ok;
}

static enum wu_error decode_jpeg(struct image_file *infile,
struct raw_state *rs, struct wu_state *state,
const enum image_event ev) {
	const int raws = (int)rs->raw.count;
	struct image_context *jpeg = &rs->jpeg;

	jpeg->state.idx = state->idx - raws;
	enum wu_error status = ev == 0
		? dec_decode(jpeg)
		: dec_callback(jpeg, ev);

	if (status <= wu_ok) {
		const enum wu_error copy_status = copy_jpeg(infile, rs, jpeg);
		if (copy_status != wu_ok) {
			status = copy_status;
		}
	}
	return status;
}
#endif

static enum wu_error raw_decode(struct image_file *infile,
const struct wu_conf *wuconf, struct wu_state *state) {
	struct raw_state *rs = infile->dec_state;
	const unsigned i = (unsigned)state->idx;
	struct wuimg *img = infile->sub_img + state->idx;

	if (i < rs->raw.count) {
		libraw_data_t *data = rs->data;
		data->rawparams.shot_select = i;

		int err = libraw_dcraw_process(data);
		if (err != LIBRAW_SUCCESS) {
			image_file_strerror_append(infile, libraw_strerror(err));
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
		img->borrowed = true;
		return wuimg_verify(img);
	} else if (rs->thumb_type == raw_thumb_jpeg) {
#ifdef WU_ENABLE_JPEG
		const enum wu_error status = decode_jpeg(infile, rs, state, 0);
		if (status != wu_ok) {
			image_file_strerror_append(infile, "Failed to "
				"decode JPEG thumbnail");
			return status;
		}
#else
		return wu_unknown_file_type;
#endif
	}
	return wu_ok;
}

static enum wu_error raw_callback(struct image_file *infile,
const struct wu_conf *wuconf, struct wu_state *state,
const enum image_event ev) {
	struct raw_state *rs = infile->dec_state;
	const int raws = (int)rs->raw.count;
#ifdef WU_ENABLE_JPEG
	if (state->idx >= raws && rs->jpeg.file.nr) {
		return decode_jpeg(infile, rs, state, ev);
	}
#endif
	return (ev == ev_subcycle)
		? raw_decode(infile, wuconf, state)
		: wu_no_change;
}

static unsigned char convert_rotate(const int flip) {
	switch (flip) {
	case 3: return 2;
	case 5: return 3;
	case 6: return 1;
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
#ifdef WU_ENABLE_JPEG
			case LIBRAW_THUMBNAIL_JPEG:
				return raw_thumb_jpeg;
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

static void read_metadata(struct wu_tree *tree, libraw_data_t *data) {
	const libraw_imgother_t *other = libraw_get_imgother(data);
	tree_add_leaf_limit(tree, "Artist", WUPTR_ARRAY(other->artist), NULL);
	tree_add_leaf_limit(tree, "Description", WUPTR_ARRAY(other->desc), NULL);
	const struct wu_tree_sap sap[] = {
		{"ISO speed", {wu_leaf_float, {.f = other->iso_speed}}},
		{"Shutter speed", {wu_leaf_float, {.f = other->shutter}}},
		{"Aperture", {wu_leaf_float, {.f = other->aperture}}},
		{"Focal length", {wu_leaf_float, {.f = other->focal_len}}},
		{"Timestamp", {wu_leaf_time, {.time = other->timestamp}}},
		{"Shot order", {wu_leaf_unsigned, {.u = other->shot_order}}},
	};
	tree_bud_leaves(tree, sap, ARRAY_LEN(sap));

	const libraw_iparams_t *idata = libraw_get_iparams(data);
	tree_add_leaf_limit(tree, "Make", WUPTR_ARRAY(idata->make), NULL);
	tree_add_leaf_limit(tree, "Model", WUPTR_ARRAY(idata->model), NULL);
	tree_add_leaf_limit(tree, "Software", WUPTR_ARRAY(idata->software), NULL);
	if (idata->dng_version) {
		tree_bud_leaf_u(tree, "DNG version", idata->dng_version);
	}

	standard_metadata(xmp_metadata, idata->xmpdata, idata->xmplen, tree);
}

static enum wu_error raw_dec(struct image_file *infile,
const struct wu_conf *wuconf) {
	struct raw_state *rs = infile->dec_state;
	libraw_data_t *data = libraw_init(0);
	if (!data) {
		return wu_alloc_error;
	}
	rs->data = data;

	unsigned char *will_crash_if_written_to = (unsigned char *)infile->map.ptr;
	int err = libraw_open_buffer(data, will_crash_if_written_to, infile->map.len);
	if (err != LIBRAW_SUCCESS) {
		return raw_error_to_wu(infile, err);
	}

	read_metadata(&infile->metadata, data);

	rs->thumb_type = unpack_thumb(wuconf, data);
	size_t nr = (size_t)(rs->thumb_type != raw_thumb_none);
	if (!wuconf->raw_prefer_thumbnail || !big_enough_thumb(data)) {
		data->params.half_size = wuconf->raw_half_size;
		data->params.use_camera_wb = 1;
		data->params.user_flip = 0;
		data->params.user_qual = 0;
		data->params.user_sat = 0;
		data->params.med_passes = 0;
		data->rawparams.use_rawspeed = 1;
		libraw_set_output_bps(data, wuconf->raw_16bit ? 16 : 8);
		libraw_set_fbdd_noiserd(data, 0);

		err = libraw_unpack(data);
		if (err != LIBRAW_SUCCESS) {
			return raw_error_to_wu(infile, err);
		}

		rs->raw.count = data->idata.raw_count;
		rs->raw.proc = calloc(rs->raw.count, sizeof(*rs->raw.proc));
		if (!rs->raw.proc) {
			return wu_alloc_error;
		}

		nr += rs->raw.count;
	}

	struct wuimg *img = alloc_sub_images(infile, nr);
	if (!img) {
		return wu_alloc_error;
	}

	const unsigned char rotate = convert_rotate(data->sizes.flip);
	for (size_t i = 0; i < infile->nr; ++i) {
		img[i].rotate = rotate;
	}

	const libraw_thumbnail_t *thumb = &data->thumbnail;
	switch (rs->thumb_type) {
	case raw_thumb_bitmap:
		img += rs->raw.count;

		img->data = (unsigned char *)thumb->thumb;
		img->w = thumb->twidth;
		img->h = thumb->theight;
		img->channels = 3;
		img->bitdepth = (thumb->tformat == LIBRAW_THUMBNAIL_BITMAP16)
			? 16 : 8;
		img->borrowed = true;
		return wuimg_verify(img);
	case raw_thumb_jpeg:
#ifdef WU_ENABLE_JPEG
		dec_src_mem(&rs->jpeg, wuptr_mem(thumb->thumb, thumb->tlength),
			NULL, &jpeg_fn);
		rs->jpeg.conf = *wuconf;
#endif
		break;
	case raw_thumb_none:
		break;
	}
	return wu_ok;
}

const struct image_fn raw_fn = {
	.mmap = true,
	.state_size = sizeof(struct raw_state),
	.dec = raw_dec,
	.callback = raw_callback,
	.end = raw_end,
};

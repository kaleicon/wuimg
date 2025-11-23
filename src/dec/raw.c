// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2021 kaleido
#include <libraw/libraw.h>

#include "wudefs.h"
#include "misc/common.h"
#include "misc/math.h"
#include "misc/metadata.h"

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
	struct wudec_image jpeg;
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

static void end_raw(struct image_file *infile) {
	struct raw_state *rs = infile->dec_state;
	for (size_t i = 0; i < rs->raw.count; ++i) {
		libraw_dcraw_clear_mem(rs->raw.proc[i]);
	}
	free(rs->raw.proc);
	if (rs->jpeg.file.sub_img) {
		wudec_free(&rs->jpeg);
		infile->nr -= rs->jpeg.file.nr;
	}
	libraw_close(rs->data);
}

#ifdef WU_ENABLE_JPEG
static struct wu_st copy_jpeg(struct image_file *infile,
const struct raw_state *rs, const struct wudec_image *jpeg) {
	struct wuimg *img = infile->sub_img;
	const size_t raw_count = rs->raw.count;
	const struct image_file *injpeg = &jpeg->file;
	if (infile->nr != raw_count + injpeg->nr) {
		img = realloc_sub_images(infile, raw_count + injpeg->nr);
		if (!img) {
			return WUERR_HERE(wu_alloc_error);
		}
	}
	int idx = jpeg->state.idx;
	memcpy(img + raw_count + idx, injpeg->sub_img + idx, sizeof(*img));
	return WU_OK;
}

static struct wu_st decode_jpeg(struct image_file *infile,
struct raw_state *rs, struct wu_state *state) {
	const int raws = (int)rs->raw.count;
	struct wudec_image *jpeg = &rs->jpeg;

	jpeg->state.idx = state->idx - raws;
	enum wu_error status = jpeg->file.nr
		? wudec_callback(jpeg, ev_subcycle)
		: wudec_decode(jpeg);

	wustr_append_wustr(&infile->errors, &jpeg->file.errors);
	jpeg->file.errors.len = 0;
	switch (status) {
	case wu_no_change: return WU_NO_CHANGE;
	case wu_ok: return copy_jpeg(infile, rs, jpeg);
	default: break;
	}
	return WUERR_HERE(status);
}
#endif

static unsigned char convert_rotate(const int flip) {
	switch (flip) {
	case 3: return 2;
	case 5: return 3;
	case 6: return 1;
	default: return 0;
	}
}

static struct wu_st event_raw(struct image_file *infile,
const struct wu_conf *wuconf, struct wu_state *state,
const enum image_event ev) {
	if (ev != ev_subcycle) {
		return WU_NO_CHANGE;
	}
	struct raw_state *rs = infile->dec_state;
	const unsigned i = (unsigned)state->idx;
	struct wuimg *img = infile->sub_img + state->idx;

	libraw_data_t *data = rs->data;
	// flip is set to 0 after processing
	img->rotate = convert_rotate(data->sizes.flip);
	if (i < rs->raw.count) {
		data->rawparams.shot_select = i;

		int err = libraw_dcraw_process(data);
		if (err != LIBRAW_SUCCESS) {
			image_file_strerror_append(infile, libraw_strerror(err));
			return wuerr(wu_decoding_error, "dcraw_process() failed");
		}

		libraw_processed_image_t *proc = libraw_dcraw_make_mem_image(
			data, NULL);
		if (!proc) {
			return WUERR_HERE(wu_alloc_error);
		}
		rs->raw.proc[i] = proc;
		if (umax(proc->width, proc->height) > wuconf->max_img_size) {
			return WUERR_HERE(wu_exceeds_size_limit);
		}

		img->data = proc->data;
		img->w = proc->width;
		img->h = proc->height;
		img->channels = (unsigned char)proc->colors;
		img->bitdepth = (unsigned char)proc->bits;
		img->borrowed = true;
	} else if (rs->thumb_type == raw_thumb_bitmap) {
		const libraw_thumbnail_t *thumb = &data->thumbnail;
		img->data = (unsigned char *)thumb->thumb;
		img->w = thumb->twidth;
		img->h = thumb->theight;
		img->channels = 3;
		img->bitdepth = (thumb->tformat == LIBRAW_THUMBNAIL_BITMAP16)
			? 16 : 8;
		img->borrowed = true;
	} else if (rs->thumb_type == raw_thumb_jpeg) {
		struct wu_st status = WUERR_HERE(wu_unknown_file_type);
#ifdef WU_ENABLE_JPEG
		status = decode_jpeg(infile, rs, state);
		if (!wu_isok(status)) {
			image_file_strerror_append(infile, "Failed to "
				"decode JPEG thumbnail");
		}
#endif
		return status;
	}
	return wuimg_verify_st(img);
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

static void read_raw_metadata(struct wutree *tree, libraw_data_t *data) {
	const libraw_imgother_t *other = libraw_get_imgother(data);
	tree_add_leaf_limit(tree, "Artist", WUPTR_ARRAY(other->artist), NULL);
	tree_add_leaf_limit(tree, "Description", WUPTR_ARRAY(other->desc), NULL);
	const struct wutree_sap sap[] = {
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

	metadata_parse(metadata_xmp, idata->xmpdata, idata->xmplen, tree);
}

static struct wu_st init_raw(struct image_file *infile,
const struct wu_conf *wuconf) {
	struct raw_state *rs = infile->dec_state;
	libraw_data_t *data = libraw_init(0);
	if (!data) {
		return WUERR_HERE(wu_alloc_error);
	}
	rs->data = data;

	unsigned char *dont_write_to_me = (unsigned char *)infile->map.ptr;
	int err = libraw_open_buffer(data, dont_write_to_me, infile->map.len);
	if (err != LIBRAW_SUCCESS) {
		return wuerr(raw_error_to_wu(infile, err),
			"initial parsing failed");
	}

	read_raw_metadata(&infile->metadata, data);

	rs->thumb_type = unpack_thumb(wuconf, data);
#ifdef WU_ENABLE_JPEG
	if (rs->thumb_type == raw_thumb_jpeg) {
		wudec_src_mem(&rs->jpeg,
			wuptr_mem(data->thumbnail.thumb, data->thumbnail.tlength),
			NULL, &jpeg_fn);
		rs->jpeg.conf = *wuconf;
	}
#endif

	infile->nr = (size_t)(rs->thumb_type != raw_thumb_none);
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
			return wuerr(raw_error_to_wu(infile, err),
				"data unpacking failed");
		}

		rs->raw.count = data->idata.raw_count;
		rs->raw.proc = calloc(rs->raw.count, sizeof(*rs->raw.proc));
		if (!rs->raw.proc) {
			return WUERR_HERE(wu_alloc_error);
		}

		infile->nr += rs->raw.count;
	}
	return WU_OK;
}

const struct image_fn raw_fn = {
	.mmap = true,
	.state_size = sizeof(struct raw_state),
	.init = init_raw,
	.event = event_raw,
	.end = end_raw,
};

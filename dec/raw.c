#include <libraw/libraw.h>

#include "../wudefs.h"
#include "../common.h"
#include "../metadata.h"

#include "jpeg.h"

static unsigned char convert_rotate(const int flip) {
	switch (flip) {
	case 3: return 2;
	case 5: return 1;
	case 6: return 3;
	default: return 0;
	}
}

static enum wu_error decode_full(struct image_file *infile,
const struct wu_conf *wuconf, libraw_data_t *data) {
	if (libraw_unpack(data) != 0) {
		return wu_decoding_error;
	} else if (!data->idata.raw_count) {
		return wu_unknown_file_type;
	}

	struct raw_img *img = alloc_sub_images(infile, data->idata.raw_count);
	if (!img) {
		return wu_alloc_error;
	}

	unsigned char rotate = convert_rotate(data->sizes.flip);
	size_t i = 0;
	for (unsigned int shot = 0; shot < infile->nr; ++shot) {
		data->params.half_size = wuconf->raw_half_size;
		data->params.user_flip = 0;
		data->params.user_qual = 0;
		data->params.shot_select = shot;
		data->params.use_rawspeed = true;

		if (libraw_dcraw_process(data)) {
			return wu_decoding_error;
		}

		libraw_processed_image_t *proc = libraw_dcraw_make_mem_image(
			data, NULL);
		if (!proc) {
			return wu_alloc_error;
		}
		if (umax(proc->width, proc->height) > wuconf->max_img_size) {
			libraw_dcraw_clear_mem(proc);
			continue;
		}

		img[i].w = proc->width;
		img[i].h = proc->height;
		img[i].channels = (unsigned char)proc->colors;
		img[i].bitdepth = (unsigned char)proc->bits;
		img[i].rotate = rotate;

		const size_t len = raw_img_addbuf(img + i);
		if (!len) {
			libraw_dcraw_clear_mem(proc);
			return wu_alloc_error;
		}

		const size_t min = zumin(len, proc->data_size);
		memcpy(img[i].data, proc->data, min);
		libraw_dcraw_clear_mem(proc);
		++i;
	}

	if (!i) {
		return wu_exceeds_size_limit;
	} else if (i < infile->nr) {
		realloc_sub_images(infile, i);
	}
	return wu_ok;
}

static enum wu_error decode_thumbnail(struct image_file *infile,
const struct wu_conf *wuconf, const libraw_data_t *data) {
	const libraw_thumbnail_t *thumb = &data->thumbnail;
	enum wu_error status;
	if (thumb->tformat == LIBRAW_THUMBNAIL_JPEG) {
		errno = 0;
		FILE *imp = fmemopen(thumb->thumb, thumb->tlength, "rb");
		if (imp) {
			FILE *orig = infile->ifp;
			infile->ifp = imp;
			status = jpeg_dec(infile, wuconf);
			infile->ifp = orig;
			fclose(imp);
		} else {
			infile->err_msg = strerror_dup(errno);
			return wu_open_error;
		}
	} else {
		struct raw_img *img = alloc_sub_images(infile, 1);
		if (!img) {
			return wu_alloc_error;
		}

		img->w = thumb->twidth;
		img->h = thumb->theight;
		img->channels = 3;
		if (thumb->tformat == LIBRAW_THUMBNAIL_BITMAP16) {
			img->bitdepth = 16;
		} else {
			img->bitdepth = 8;
		}

		const size_t len = raw_img_addbuf(img);
		if (!len) {
			return wu_alloc_error;
		}

		const size_t min = zumin(len, thumb->tlength);
		memcpy(img->data, thumb->thumb, min);
		status = wu_ok;
	}
	infile->sub_img[0].id = strdup("thumbnail");
	infile->sub_img[0].rotate = convert_rotate(data->sizes.flip);
	return status;
}

static bool prefer_thumbnail(const struct wu_conf *wuconf, libraw_data_t *data) {
	if (!wuconf->raw_prefer_thumbnail || data->idata.raw_count != 1) {
		return false;
	}

	const libraw_thumbnail_t *thumb = &data->thumbnail;
	const unsigned int img_dims = umin(data->sizes.width, data->sizes.height);
	const unsigned int thumb_dims = umax(thumb->twidth, thumb->theight);
	if (thumb_dims < img_dims / 2) {
		return false;
	}

	if (thumb_dims <= wuconf->max_img_size) {
		switch (libraw_unpack_thumb(data)) {
		case LIBRAW_SUCCESS:
			switch (data->thumbnail.tformat) {
			case LIBRAW_THUMBNAIL_JPEG:
#ifndef DEC_JPEG
				return false;
#endif
			case LIBRAW_THUMBNAIL_BITMAP:
			case LIBRAW_THUMBNAIL_BITMAP16:
				return true;
			default:
				break;
			}
			break;
		case LIBRAW_NO_THUMBNAIL:
		case LIBRAW_UNSUPPORTED_THUMBNAIL:
			break;
		default:
			break;
		}
	}
	return false;
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
		const union wu_leaf value = {.u = idata->dng_version};
		tree_bud_leaf(tree, "DNG version", wu_leaf_unsigned, value);
	}

	standard_metadata(xmp_metadata, idata->xmpdata, idata->xmplen, tree);
}

enum wu_error raw_dec(struct image_file *infile, const struct wu_conf *wuconf) {
	libraw_data_t *data = libraw_init(0);
	if (!data) {
		return wu_alloc_error;
	}

	struct mmap_file map = mmap_stream(infile->ifp);
	if (map.data == MAP_FAILED) {
		libraw_close(data);
		return wu_alloc_error;
	}

	if (libraw_open_buffer(data, map.data, map.len)) {
		libraw_close(data);
		munmap_stream(map);
		return wu_open_error;
	}

	read_metadata(&infile->metadata, data);

	enum wu_error status;
	if (prefer_thumbnail(wuconf, data)) {
		status = decode_thumbnail(infile, wuconf, data);
	} else {
		status = decode_full(infile, wuconf, data);
	}
	libraw_close(data);
	munmap_stream(map);
	return status;
}

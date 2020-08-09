#include <libraw/libraw.h>

#include "wudefs.h"
#include "common.h"

#include "dec_jpeg.h"

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

		const size_t len = img[i].w * img[i].h * img[i].channels
			* (img[i].bitdepth / 8);
		if (len > proc->data_size) {
			libraw_dcraw_clear_mem(proc);
			return wu_decoding_error;
		}

		img[i].data = malloc(len);
		if (!img[i].data) {
			libraw_dcraw_clear_mem(proc);
			return wu_alloc_error;
		}

		memcpy(img[i].data, proc->data, len);
		libraw_dcraw_clear_mem(proc);
		++i;
	}

	if (!i) {
		return wu_exceeded_size_limit;
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
			infile->err_msg = strdup(strerror(errno));
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

		const size_t len = img->w * img->h * img->channels
			* (img->bitdepth / 8);
		if (len <= thumb->tlength) {
			img->data = malloc(len);
			if (!img->data) {
				return wu_alloc_error;
			}
			memcpy(img->data, thumb->thumb, len);
		} else {
			return wu_decoding_error;
		}
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

static void read_metadata(FILE *stream, const libraw_data_t *data) {
	const libraw_imgother_t *other = &data->other;
	const size_t artist_len = strnlen(other->artist, sizeof(other->artist));
	const size_t desc_len = strnlen(other->desc, sizeof(other->desc));
	if (artist_len) {
		print_unsafe_data(other->artist, artist_len, "Artist", true, stream);
	}
	if (desc_len) {
		print_unsafe_data(other->desc, desc_len, "Description", true, stream);
	}


	const libraw_iparams_t *idata = &data->idata;
	const size_t make_len = strnlen(idata->make, sizeof(idata->make));
	const size_t model_len = strnlen(idata->model, sizeof(idata->model));
	const size_t soft_len = strnlen(idata->software, sizeof(idata->software));
	if (make_len) {
		print_unsafe_data(idata->make, make_len, "Make", true, stream);
	}
	if (model_len) {
		print_unsafe_data(idata->model, model_len, "Model", true, stream);
	}
	if (soft_len) {
		print_unsafe_data(idata->software, soft_len, "Software", true, stream);
	}
	if (idata->dng_version) {
		fprintf(stream, "DNG Version: %u\n", idata->dng_version);
	}

	fprintf(stream, "ISO speed: %g\n"
		"Shutter speed: %g\n"
		"Aperture: %g\n"
		"Focal length: %g\n"
		"Shot order: %u\n"
		"Flash exposure compensation: %g\n",
		other->iso_speed, other->shutter, other->aperture,
		other->focal_len, other->shot_order, other->FlashEC);
}

enum wu_error raw_dec(struct image_file *infile, const struct wu_conf *wuconf) {
	libraw_data_t *data = libraw_init(0);
	if (!data) {
		return wu_alloc_error;
	}

	size_t size = 0;
	unsigned char *buf = read_file_to_mem(infile->ifp, &size);
	if (!buf) {
		libraw_close(data);
		return wu_alloc_error;
	}

	if (libraw_open_buffer(data, buf, size)) {
		libraw_close(data);
		free(buf);
		return wu_open_error;
	}

	read_metadata(infile->meta.fp, data);

	const bool use_thumb = prefer_thumbnail(wuconf, data);
	enum wu_error status;
	if (use_thumb) {
		status = decode_thumbnail(infile, wuconf, data);
	} else {
		status = decode_full(infile, wuconf, data);
	}
	libraw_close(data);
	free(buf);
	return status;
}

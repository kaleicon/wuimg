// SPDX-License-Identifier: 0BSD
#include <openjpeg-2.1/openjpeg.h>

#include "misc/bit.h"
#include "misc/common.h"
#include "raster/unpack.h"
#include "wudefs.h"

static void monkey_trouble_handler(const char *msg, void *userdata) {
	image_file_strerror_append(userdata, msg);
}

static OPJ_SIZE_T file_read(void *buf, const OPJ_SIZE_T len, void *file) {
	FILE *ifp = file;
	const size_t count = fread(buf, 1, len, ifp);
	if (!count) {
		return (OPJ_SIZE_T)-1;
	}
	return (OPJ_SIZE_T)count;
}

static OPJ_OFF_T file_skip(const OPJ_OFF_T offset, void *file) {
	FILE *ifp = file;
	return fseek(ifp, offset, SEEK_CUR) == -1 ? -1 : offset;
}

static OPJ_BOOL file_seek(const OPJ_OFF_T offset, void *file) {
	FILE *ifp = file;
	return fseek(ifp, offset, SEEK_SET) != -1;
}

static opj_stream_t setup_jp2_stream(FILE *ifp) {
	fseek(ifp, 0, SEEK_END);
	const long size = ftell(ifp);
	fseek(ifp, 0, SEEK_SET);

	opj_stream_t *stream = opj_stream_default_create(true);
	if (stream) {
		opj_stream_set_user_data(stream, ifp, NULL);
		opj_stream_set_user_data_length(stream, (size_t)size);
		opj_stream_set_read_function(stream, file_read);
		opj_stream_set_skip_function(stream, file_skip);
		opj_stream_set_seek_function(stream, file_seek);
	}
	return stream;
}

static enum wu_error dec_wrap(struct wuimg *img, const opj_image_t *jp2) {
	if (jp2->numcomps > 4) {
		return wu_unsupported_feature;
	}
	img->channels = (unsigned char)jp2->numcomps;
	img->bitdepth = 32;

	struct image_planes *planes = wuimg_plane_init(img);
	if (!planes) {
		return wu_alloc_error;
	}

	struct plane_info *p = planes->p;
	const opj_image_comp_t *comps = jp2->comps;
	for (uint8_t j = 0; j < img->channels; ++j) {
		p[j].x.subsamp = (unsigned char)comps[j].dx;
		p[j].y.subsamp = (unsigned char)comps[j].dy;
	}

	if (jp2->icc_profile_buf) {
		color_space_set_icc_copy(&img->cs, jp2->icc_profile_buf,
			jp2->icc_profile_len);
	}

	switch (jp2->color_space) {
	case OPJ_CLRSPC_UNKNOWN:
	case OPJ_CLRSPC_UNSPECIFIED:
		if (jp2->icc_profile_buf) {
			break;
		}
		break;
	case OPJ_CLRSPC_SRGB:
	case OPJ_CLRSPC_GRAY:
		img->cs.matrix = cicp_matrix_rgb;
		break;
	case OPJ_CLRSPC_SYCC:
	case OPJ_CLRSPC_EYCC: // What is EYCC, even?
		img->cs.matrix = cicp_matrix_bt601_7;
		img->cs.limited = true;
		break;
	case OPJ_CLRSPC_CMYK:
		img->alpha = alpha_key;
		img->cs.matrix = cicp_matrix_rgb;
		break;
	default:
		return wu_unsupported_feature;
	}

	const enum wu_error st = wuimg_verify(img);
	if (st == wu_ok) {
		img->borrowed = true;
		img->data = (uint8_t *)-1;
		for (uint8_t z = 0; z < img->channels; ++z) {
			const enum pix_attr attr = comps[z].sgnd
				? pix_signed : pix_normal;
			p[z].ptr = (uint8_t *)comps[z].data;

			const struct remap_info scaler = remap_scale_info(
				bit_set32(comps[z].prec), img->bitdepth, attr);
			remap_scale(p[z].ptr, p[z].ptr, p[z].w * p[z].h,
				scaler);
		}
	}
	return st;
}

static void set_limits(opj_codec_t *dec, opj_image_t *jp2) {
	if (jp2->numcomps > 4) {
		const uint32_t comps[4] = {0,1,2,3};
		opj_set_decoded_components(dec, 4, comps, false);
	}
}

static enum wu_error get_image_size(struct wuimg *img,
const struct wu_conf *wuconf, const opj_image_t *jp2) {
	for (OPJ_UINT32 i = 0; i < jp2->numcomps; ++i) {
		const opj_image_comp_t *comp = jp2->comps + i;
		/* Offsets mess with our plane subsample math, so jump ship if
		 * they're not exactly divisible.
		 * The proper solution would be compositing components. */
		if (comp->x0 % comp->dx || comp->y0 % comp->dy) {
			return wu_unsupported_feature;
		}
	}
	img->w = jp2->x1 - jp2->x0;
	img->h = jp2->y1 - jp2->y0;
	return wuimg_exceeds_limit(img, wuconf) ? wu_exceeds_size_limit : wu_ok;
}

static enum wu_error jpeg2000_dec(struct image_file *infile,
const struct wu_conf *wuconf, const OPJ_CODEC_FORMAT format) {
	struct wuimg *img = infile->sub_img;
	if (img) {
		wuimg_clear(img);
	} else {
		img = alloc_sub_images(infile, 1);
		if (!img) {
			return wu_alloc_error;
		}
	}

	opj_codec_t *dec = opj_create_decompress(format);
	if (!dec) {
		return wu_alloc_error;
	}

	//opj_set_info_handler(dec, monkey_trouble_handler, infile);
	opj_set_warning_handler(dec, monkey_trouble_handler, infile);
	opj_set_error_handler(dec, monkey_trouble_handler, infile);

	opj_dparameters_t params;
	opj_set_default_decoder_parameters(&params);

	opj_image_t *jp2 = NULL;
	enum wu_error st = wu_invalid_params;
	if (opj_setup_decoder(dec, &params)) {
		if (opj_has_thread_support()) {
			opj_codec_set_threads(dec, (int)num_cpus());
		}
		opj_stream_t *stream = setup_jp2_stream(infile->ifp);
		if (stream) {
			if (opj_read_header(stream, dec, &jp2)) {
				st = get_image_size(img, wuconf, jp2);
				if (st == wu_ok) {
					set_limits(dec, jp2);
					if (opj_decode(dec, stream, jp2)) {
						opj_end_decompress(dec, stream);
					} else {
						st = wu_decoding_error;
					}
				}
			} else {
				st = wu_invalid_header;
			}
			opj_stream_destroy(stream);
		}
	}
	opj_destroy_codec(dec);

	if (st == wu_ok) {
		st = dec_wrap(img, jp2);
		infile->dec_state = jp2;
	} else if (jp2) {
		opj_image_destroy(jp2);
	}
	return st;
}

static void jpeg2000_end(struct image_file *infile) {
	opj_image_destroy(infile->dec_state);
}

static enum wu_error jp2_dec(struct image_file *infile,
const struct wu_conf *wuconf) {
	return jpeg2000_dec(infile, wuconf, OPJ_CODEC_JP2);
}

static enum wu_error j2k_dec(struct image_file *infile,
const struct wu_conf *wuconf) {
	return jpeg2000_dec(infile, wuconf, OPJ_CODEC_J2K);
}

const struct image_fn jp2_fn = {.dec = jp2_dec, .end = jpeg2000_end};
const struct image_fn j2k_fn = {.dec = j2k_dec, .end = jpeg2000_end};

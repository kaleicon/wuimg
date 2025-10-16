// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2019 kaleido
#include <openjpeg.h>

#include "misc/bit.h"
#include "misc/common.h"
#include "misc/math.h"
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

static struct wu_st dec_wrap(struct wuimg *img, const opj_image_t *jp2) {
	img->channels = (unsigned char)u32min(jp2->numcomps, 4);
	img->bitdepth = 32;

	struct image_planes *planes = wuimg_plane_init(img);
	if (!planes) {
		return WUERR_HERE(wu_alloc_error);
	}

	const opj_image_comp_t *comps = jp2->comps;
	struct plane_info *p = planes->p;
	for (uint8_t j = 0; j < img->channels; ++j) {
		p[j].x.subsamp = (unsigned char)(comps[j].dx << comps[j].factor);
		p[j].y.subsamp = (unsigned char)(comps[j].dy << comps[j].factor);
	}

	if (jp2->icc_profile_buf) {
		color_space_set_icc_copy(&img->cs, jp2->icc_profile_buf,
			jp2->icc_profile_len);
	}

	struct wu_st st = WU_OK;
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
		st.msg = "unknown colorspace";
	}

	const enum wu_error e = wuimg_verify(img);
	if (e == wu_ok) {
		img->borrowed = true;
		img->data = (uint8_t *)-1;
		for (uint8_t z = 0; z < img->channels; ++z) {
			const enum pix_attr attr = comps[z].sgnd
				? pix_signed : pix_normal;
			p[z].ptr = (uint8_t *)comps[z].data;

			struct remap_info scaler = remap_scale_info(
				bit_set32(comps[z].prec),
				img->bitdepth, attr);
			remap_scale(p[z].ptr, p[z].ptr,
				p[z].w * p[z].h, scaler);
		}
	} else {
		st = WUERR_HERE(e);
	}
	return st;
}

static struct wu_st set_decode_size(opj_codec_t *dec, const opj_image_t *jp2,
struct wuimg *img, const struct wu_conf *wuconf) {
	OPJ_UINT32 ch = jp2->numcomps;
	if (jp2->numcomps > 4) {
		const OPJ_UINT32 comps[4] = {0,1,2,3};
		opj_set_decoded_components(dec, ARRAY_LEN(comps), comps,
			OPJ_FALSE);
		ch = ARRAY_LEN(comps);
	}

	img->w = jp2->x1 - jp2->x0;
	img->h = jp2->y1 - jp2->y0;
	if (!zumin(img->w, img->h)) {
		return wuerr(wu_no_image_data, "image has zero width and height");
	}
	const size_t div = (zumax(img->w, img->h) - 1) / wuconf->max_img_size;
	OPJ_UINT32 factor = 0;
	if (div) {
		/* Image exceeds our size limit, so try decoding at a reduced
		 * resolution. */
		factor = (OPJ_UINT32)zulog2(div) + 1;
		if (!opj_set_decoded_resolution_factor(dec, factor)) {
			return wuerr(wu_exceeds_size_limit, "image exceeds"
				" dimension limit and we couldn't decode at a"
				" lower resolution");
		}
	}
	for (OPJ_UINT32 i = 0; i < ch; ++i) {
		const opj_image_comp_t *comp = jp2->comps + i;
		/* Offsets mess with our plane subsample math, so jump ship if
		 * they're not exactly divisible.
		 * The proper solution would be adding an offset field, and
		 * making our life miserable everywhere around the program. */
		if (comp->x0 % comp->dx || comp->y0 % comp->dy) {
			return wuerr(wu_unsupported_feature,
				"subsampled planes with offset");
		}
		/* Naively shifting down the image size by the resolution
		 * factor also screws with subsampling math, but shifting up
		 * the subsamp factor instead works wonders. */
		if (comp->dx << factor > 0xff || comp->dy << factor > 0xff) {
			return wuerr(wu_unsupported_feature,
				"subsampling factor too high");
		}
	}
	return WU_OK;
}

static struct wu_st jpeg2000_dec(struct image_file *infile,
const struct wu_conf *wuconf, const OPJ_CODEC_FORMAT format) {
	opj_codec_t *dec = opj_create_decompress(format);
	if (!dec) {
		return WUERR_HERE(wu_alloc_error);
	}

	//opj_set_info_handler(dec, monkey_trouble_handler, infile);
	opj_set_warning_handler(dec, monkey_trouble_handler, infile);
	opj_set_error_handler(dec, monkey_trouble_handler, infile);

	opj_dparameters_t params;
	opj_set_default_decoder_parameters(&params);

	opj_image_t *jp2 = NULL;
	struct wu_st st = WUERR_HERE(wu_invalid_params);
	if (opj_setup_decoder(dec, &params)) {
		if (opj_has_thread_support()) {
			opj_codec_set_threads(dec, (int)num_cpus());
		}
		opj_stream_t *stream = setup_jp2_stream(infile->ifp);
		if (stream) {
			if (opj_read_header(stream, dec, &jp2)) {
				st = set_decode_size(dec, jp2, infile->sub_img,
					wuconf);
				if (wu_isok(st)) {
					if (opj_decode(dec, stream, jp2)) {
						opj_end_decompress(dec, stream);
					} else {
						st = WUERR_HERE(wu_decoding_error);
					}
				}
			} else {
				st = WUERR_HERE(wu_invalid_header);
			}
			opj_stream_destroy(stream);
		}
	}
	opj_destroy_codec(dec);

	if (wu_isok(st)) {
		infile->dec_state = jp2;
		st = dec_wrap(infile->sub_img, jp2);
	} else if (jp2) {
		opj_image_destroy(jp2);
	}
	return st;
}

static void jpeg2000_end(struct image_file *infile) {
	opj_image_destroy(infile->dec_state);
}

static struct wu_st init_jp2(struct image_file *infile,
const struct wu_conf *wuconf) {
	return jpeg2000_dec(infile, wuconf, OPJ_CODEC_JP2);
}
static struct wu_st init_j2k(struct image_file *infile,
const struct wu_conf *wuconf) {
	return jpeg2000_dec(infile, wuconf, OPJ_CODEC_J2K);
}

const struct image_fn jp2_fn = {
	.alloc_single = true,
	.init = init_jp2,
	.end = jpeg2000_end,
};
const struct image_fn j2k_fn = {
	.alloc_single = true,
	.init = init_j2k,
	.end = jpeg2000_end,
};

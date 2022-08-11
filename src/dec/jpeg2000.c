#include <stdlib.h>
#include <string.h>

#include <openjpeg-2.1/openjpeg.h>

#include "raster/strip.h"
#include "wudefs.h"

static void clean_dec_state(struct image_file *infile) {
	if (infile->dec_state) {
		opj_image_destroy(infile->dec_state);
		infile->dec_state = NULL;
	}
}

static void monkey_trouble_handler(const char *msg, void *userdata) {
	image_file_error_append(userdata, msg);
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

static enum wu_error dec_wrap(struct raw_img *img, const opj_image_t *jp2) {
	if (jp2->numcomps > 4) {
		return wu_unsupported_feature;
	}
	img->channels = (unsigned char)jp2->numcomps;
	img->bitdepth = 32;

	struct image_planes *planes = raw_img_plane_init(img);
	if (!planes) {
		return wu_alloc_error;
	}
	struct plane_info *p = planes->p;

	const opj_image_comp_t *comps = jp2->comps;
	bool equal_size = true;
	struct scale_info scaler[4];
	for (uint8_t j = 0; j < img->channels; ++j) {
		if (j && equal_size) {
			equal_size = comps[0].dx == comps[j].dx
				&& comps[0].dy == comps[j].dy;
		}
		p[j].x.subsamp = (unsigned char)comps[j].dx;
		p[j].y.subsamp = (unsigned char)comps[j].dy;
		scaler[j] = strip_scale_info(~0u >> (32 - comps[j].prec),
			img->bitdepth);
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
		if (equal_size) {
	case OPJ_CLRSPC_SRGB:
	case OPJ_CLRSPC_GRAY:
			img->cs.matrix = cicp_matrix_rgb;
		} else {
			// fallthrough
	case OPJ_CLRSPC_SYCC:
	case OPJ_CLRSPC_EYCC: // What is EYCC, even?
			img->cs.matrix = cicp_matrix_bt601_7;
			img->cs.limited = true;
		}
		break;
	case OPJ_CLRSPC_CMYK:
		img->alpha = alpha_key;
		img->cs.matrix = cicp_matrix_rgb;
		break;
	default:
		return wu_unsupported_feature;
	}

	const enum wu_error st = raw_img_verify(img);
	if (st == wu_ok) {
		img->data = IMG_DATA_BORROWED;
		for (uint8_t z = 0; z < img->channels; ++z) {
			p[z].ptr = (uint8_t *)jp2->comps[z].data;
			strip_scale(p[z].ptr, p[z].ptr, p[z].w * p[z].h,
				scaler[z], jp2->comps[z].sgnd);
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

static enum wu_error jpeg2000_dec(struct image_file *infile,
const struct wu_conf *wuconf, const OPJ_CODEC_FORMAT format) {
	struct raw_img *img = infile->sub_img;
	if (img) {
		raw_img_clear(img);
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
			opj_codec_set_threads(dec, opj_get_num_cpus());
		}
		opj_stream_t *stream = setup_jp2_stream(infile->ifp);
		if (stream) {
			if (opj_read_header(stream, dec, &jp2)) {
				set_limits(dec, jp2);
				if (opj_decode(dec, stream, jp2)) {
					opj_end_decompress(dec, stream);
					img->w = jp2->x1 - jp2->x0;
					img->h = jp2->y1 - jp2->y0;
					st = raw_img_exceeds_limit(img, wuconf)
						? wu_exceeds_size_limit : wu_ok;
				} else {
					st = wu_decoding_error;
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

static enum wu_error jpeg2000_callback(struct image_file *infile,
const struct wu_conf *wuconf, struct wu_state *state,
const enum image_event ev, const OPJ_CODEC_FORMAT format) {
	if (ev == ev_upscale) {
		if (state->zoom >= 1) {
			clean_dec_state(infile);
			state->zoom *= infile->sub_img->dec_scale;
			return jpeg2000_dec(infile, wuconf, format);
		}
	}
	clean_dec_state(infile);
	return wu_no_change;
}

enum wu_error jp2_callback(struct image_file *infile,
const struct wu_conf *wuconf, struct wu_state *state,
const enum image_event ev) {
	return jpeg2000_callback(infile, wuconf, state, ev, OPJ_CODEC_JP2);
}

enum wu_error j2k_callback(struct image_file *infile,
const struct wu_conf *wuconf, struct wu_state *state,
const enum image_event ev) {
	return jpeg2000_callback(infile, wuconf, state, ev, OPJ_CODEC_J2K);
}

enum wu_error jp2_dec(struct image_file *infile, const struct wu_conf *wuconf) {
	return jpeg2000_dec(infile, wuconf, OPJ_CODEC_JP2);
}

enum wu_error j2k_dec(struct image_file *infile, const struct wu_conf *wuconf) {
	return jpeg2000_dec(infile, wuconf, OPJ_CODEC_J2K);
}

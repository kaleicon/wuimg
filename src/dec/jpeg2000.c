#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <inttypes.h>

#include <openjpeg-2.1/openjpeg.h>

#include "wudefs.h"

static void monkey_trouble_handler(const char *msg, void *userdata) {
	image_file_error_append(userdata, msg);
}

static OPJ_SIZE_T file_read(void *buf, const OPJ_SIZE_T len, void *file) {
	FILE *ifp = file;
	unsigned char *out = (unsigned char *)buf;
	const size_t count = fread(out, 1, len, ifp);
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
	return fseek(ifp, offset, SEEK_SET) == -1 ? OPJ_FALSE : OPJ_TRUE;
}

static opj_stream_t setup_jp2_stream(FILE *ifp) {
	fseek(ifp, 0, SEEK_END);
	const OPJ_UINT64 size = (OPJ_UINT64)ftell(ifp);
	fseek(ifp, 0, SEEK_SET);

	opj_stream_t *stream = opj_stream_default_create(OPJ_TRUE);
	if (stream) {
		opj_stream_set_user_data(stream, ifp, NULL);
		opj_stream_set_user_data_length(stream, size);
		opj_stream_set_read_function(stream, file_read);
		opj_stream_set_skip_function(stream, file_skip);
		opj_stream_set_seek_function(stream, file_seek);
	}
	return stream;
}

static OPJ_UINT32 log_fit_factor(unsigned int contain_w, unsigned int contain_h,
OPJ_UINT32 w, OPJ_UINT32 h) {
	return (OPJ_UINT32)ulog2(umax(w / contain_w, h / contain_h));
}

static enum wu_error join_components(struct raw_img *img,
const opj_image_t *jp2) {
	opj_image_comp_t *comps = jp2->comps;

	const OPJ_UINT32 prec = comps[0].prec;
	if (prec > 16) {
		return wu_unsupported_feature;
	}

	const OPJ_UINT32 ch = jp2->numcomps;
	img->w = comps[0].w;
	img->h = comps[0].h;
	img->channels = (unsigned char)ch;
	img->bitdepth = (prec > 8) ? 16 : 8;
	img->attr = (comps[0].sgnd) ? pix_signed : pix_normal;

	switch (jp2->color_space) {
	case OPJ_CLRSPC_CMYK:
		img->alpha = alpha_key;
		// fallthrough
	case OPJ_CLRSPC_UNKNOWN:
	case OPJ_CLRSPC_UNSPECIFIED:
	case OPJ_CLRSPC_SRGB:
	case OPJ_CLRSPC_GRAY:
		img->cs.matrix = cicp_matrix_rgb;
		break;
	case OPJ_CLRSPC_SYCC:
		img->cs.matrix = cicp_matrix_bt601_7;
		img->cs.limited = true;
		break;
	default:
		return wu_unsupported_feature;
	}

	struct plane_info *p;
	if (img->data) {
		free(img->data);
		p = img->u.planes->p;
	} else {
		struct image_planes *planes = raw_img_plane_init(img);
		if (!planes) {
			return wu_alloc_error;
		}
		p = planes->p;
	}

	for (OPJ_UINT32 j = 0; j < ch; ++j) {
		p[j].x.subsamp = (unsigned char)comps[j].dx;
		p[j].y.subsamp = (unsigned char)comps[j].dy;
	}

	const enum wu_error st = raw_img_alloc(img);
	if (st != wu_ok) {
		return st;
	}

	const OPJ_INT32 scale = (((1 << img->bitdepth) - 1) << img->bitdepth)
		/ ((1 << prec) - 1) + 1;
	for (OPJ_UINT32 j = 0; j < ch; ++j) {
		void *ptr = p[j].ptr;
		for (size_t i = 0; i < comps[j].w * comps[j].h; ++i) {
			const OPJ_INT32 pix = (comps[j].data[i] * scale)
				>> img->bitdepth;
			if (prec > 8) {
				((uint16_t *)ptr)[i] = (uint16_t)pix;
			} else {
				((uint8_t *)ptr)[i] = (uint8_t)pix;
			}
		}
	}
	return wu_ok;
}

static enum wu_error jpeg2000_dec(struct image_file *infile,
const struct wu_conf *wuconf, const OPJ_CODEC_FORMAT format,
const bool callback) {
	opj_codec_t *dec = opj_create_decompress(format);
	if (!dec) {
		return wu_alloc_error;
	}
	opj_set_warning_handler(dec, monkey_trouble_handler, infile);
	opj_set_error_handler(dec, monkey_trouble_handler, infile);

	opj_dparameters_t params;
	opj_set_default_decoder_parameters(&params);

	if (!opj_setup_decoder(dec, &params)) {
		opj_destroy_codec(dec);
		return wu_invalid_params;
	}

	if (opj_has_thread_support() == OPJ_TRUE) {
		opj_codec_set_threads(dec, opj_get_num_cpus());
	}

	opj_stream_t *stream = setup_jp2_stream(infile->ifp);
	if (!stream) {
		opj_destroy_codec(dec);
	}

	opj_image_t *jp2;
	if (!opj_read_header(stream, dec, &jp2)) {
		opj_stream_destroy(stream);
		opj_destroy_codec(dec);
		return wu_invalid_header;
	}

	const OPJ_UINT32 tex_fit = log_fit_factor(wuconf->max_img_size,
		wuconf->max_img_size, jp2->comps[0].w, jp2->comps[0].h);
	OPJ_UINT32 screen_fit;
	if (callback || !wuconf->partial_decode) {
		screen_fit = tex_fit;
		if (tex_fit) {
			image_file_error_append(infile, "Warning: JP2 exceeds "
				"the max image size. Output will be downscaled.");
		}
	} else {
		screen_fit = log_fit_factor((unsigned)wuconf->fb.w,
			(unsigned)wuconf->fb.h,
			jp2->comps[0].w, jp2->comps[0].h);
	}

	if (screen_fit) {
		opj_set_decoded_resolution_factor(dec, screen_fit);
	}

	const bool success = opj_decode(dec, stream, jp2);
	opj_stream_destroy(stream);
	opj_destroy_codec(dec);
	if (!success) {
		opj_image_destroy(jp2);
		return wu_decoding_error;
	}

	enum wu_error status = wu_ok;
	struct raw_img *img = infile->sub_img;
	if (!img) {
		img = alloc_sub_images(infile, 1);
		if (!img) {
			status = wu_alloc_error;
		}
	}

	if (status == wu_ok) {
		status = join_components(img, jp2);
		if (status == wu_ok) {
			const OPJ_UINT32 diff_fit = screen_fit - tex_fit;
			if (diff_fit) {
				img->dec_scale = 1.0f / (1 << diff_fit);
				infile->events = ev_upscale;
			} else {
				img->dec_scale = 1;
				infile->events = 0;
			}
		}
	}
	opj_image_destroy(jp2);
	return status;
}

static enum wu_error jpeg2000_callback(struct image_file *infile,
const struct wu_conf *wuconf, struct wu_state *state,
const enum image_event ev, const OPJ_CODEC_FORMAT format) {
	if (ev == ev_upscale) {
		if (state->zoom >= 1) {
			state->zoom *= infile->sub_img->dec_scale;
			return jpeg2000_dec(infile, wuconf, format, true);
		}
	}
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
	return jpeg2000_dec(infile, wuconf, OPJ_CODEC_JP2, false);
}

enum wu_error j2k_dec(struct image_file *infile, const struct wu_conf *wuconf) {
	return jpeg2000_dec(infile, wuconf, OPJ_CODEC_J2K, false);
}

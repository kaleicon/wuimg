#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <inttypes.h>

#include <openjpeg-2.1/openjpeg.h>

#include "../wudefs.h"
#include "../common.h"

static void monkey_trouble_handler(const char *msg, void *__unused_userdata) {
	(void)__unused_userdata;
	puts(msg);
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
	opj_stream_set_user_data(stream, ifp, NULL);
	opj_stream_set_user_data_length(stream, size);
	opj_stream_set_read_function(stream, file_read);
	opj_stream_set_skip_function(stream, file_skip);
	opj_stream_set_seek_function(stream, file_seek);
	return stream;
}

static OPJ_UINT32 log_fit_factor(unsigned int contain_w, unsigned int contain_h,
OPJ_UINT32 w, OPJ_UINT32 h) {
	return (OPJ_UINT32)ulog2(umax(w / contain_w, h / contain_h));
}

static enum wu_error join_components(struct raw_img *img,
const opj_image_t *jp2) {
	img->w = jp2->comps[0].w;
	img->h = jp2->comps[0].h;
	img->channels = (unsigned char)jp2->numcomps;
	img->bitdepth = (unsigned char)jp2->comps[0].prec;

	if (!raw_img_addbuf(img)) {
		return wu_alloc_error;
	}

	const size_t ch = img->channels;
	for (size_t j = 0; j < ch; ++j) {
		for (size_t i = 0; i < img->w * img->h; ++i) {
			img->data[i*ch + j] = (unsigned char)
				jp2->comps[j].data[i];
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
	opj_set_warning_handler(dec, monkey_trouble_handler, NULL);
	opj_set_error_handler(dec, monkey_trouble_handler, NULL);

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
	opj_image_t *jp2;
	if (!opj_read_header(stream, dec, &jp2)) {
		opj_stream_destroy(stream);
		opj_destroy_codec(dec);
		return wu_invalid_header;
	}

	const OPJ_UINT32 tex_fit = log_fit_factor(wuconf->max_img_size,
		wuconf->max_img_size, jp2->comps[0].w, jp2->comps[0].h);
	OPJ_UINT32 screen_fit;
	if (callback) {
		screen_fit = tex_fit;
		if (tex_fit) {
			puts("Warning: JP2 exceeds the max image size. Output "
				"will be downscaled.");
		}
	} else {
		screen_fit = log_fit_factor(wuconf->fb.w, wuconf->fb.h,
			jp2->comps[0].w, jp2->comps[0].h);
	}

	if (screen_fit) {
		opj_set_decoded_resolution_factor(dec, screen_fit);
	}

	if (jp2->numcomps > 4) {
		puts("Warning: JP2 colorspace uses more than 4 channels. The "
			"result will be a dumb attempt at showing something.");
		const OPJ_UINT32 comps[] = {0, 1, 2, 3};
		opj_set_decoded_components(dec, ARRAY_LEN(comps), comps,
			OPJ_FALSE);
	}

	if (!opj_decode(dec, stream, jp2)) {
		opj_image_destroy(jp2);
		opj_stream_destroy(stream);
		opj_destroy_codec(dec);
		return wu_decoding_error;
	}

	opj_end_decompress(dec, stream);
	opj_stream_destroy(stream);

	struct raw_img *img;
	if (infile->sub_img) {
		img = infile->sub_img;
	} else {
		img = alloc_sub_images(infile, 1);
	}

	enum wu_error status;
	if (img) {
		status = join_components(img, jp2);
		if (status == wu_ok) {
			if (screen_fit > tex_fit) {
				img->dec_scale = 1.0f / (1 << screen_fit);
				infile->events = ev_upscale;
			} else {
				infile->events = 0;
			}
		}
	} else {
		status = wu_alloc_error;
	}
	opj_image_destroy(jp2);
	opj_destroy_codec(dec);
	return status;
}

static enum wu_error jpeg2000_callback(struct image_file *infile,
const struct wu_conf *wuconf, struct wu_state *state,
const enum image_event ev, const OPJ_CODEC_FORMAT format) {
	if (ev == ev_upscale && state->zoom >= 1) {
		struct raw_img *img = infile->sub_img;
		free(img->data);
		state->zoom *= img->dec_scale;
		img->dec_scale = 1;
		return jpeg2000_dec(infile, wuconf, format, true);
	} else if (ev == 0) {
		infile->events = 0;
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

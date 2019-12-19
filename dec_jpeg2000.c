#include <stdlib.h>

#include <openjpeg-2.3/openjpeg.h>

#include "wudefs.h"

enum wu_error_type join_components(struct raw_img *img, const opj_image_t *jp2) {
	img->w = jp2->comps[0].w;
	img->h = jp2->comps[0].h;
	img->channels = (unsigned char)jp2->numcomps;
	img->bitdepth = (unsigned char)jp2->comps[0].prec;
	img->true_bitdepth = (unsigned char)jp2->comps[0].prec;

	switch (jp2->color_space) {
	case OPJ_CLRSPC_UNKNOWN:
		puts("Unknown and unsupported color space.");
		return wu_unsupported_feature;
	case OPJ_CLRSPC_UNSPECIFIED:
		puts("Colorspace not specified in codestream. Will guess.");
		break;
	case OPJ_CLRSPC_SRGB:
		if (img->channels < 3) {
			printf("Colorspace is sRGB but there are %hhu channels\n",
				img->channels);
			return wu_unsupported_feature;
		}
		break;
	case OPJ_CLRSPC_GRAY:
		if (img->channels > 2) {
			printf("Colorspace is GRAY but there are %hhu channels\n",
				img->channels);
			return wu_unsupported_feature;
		}
		break;
	default:
		printf("Unsupported colorspace %d\n", jp2->color_space);
		return wu_unsupported_feature;
	}

	const size_t dimensions = img->w * img->h;
	const size_t ch = img->channels;
	img->data = malloc(dimensions * ch * img->bitdepth/8);
	if (!img->data) {
		return wu_alloc_error;
	}

	for (size_t i = 0; i < dimensions; ++i) {
		for (size_t j = 0; j < ch; ++j) {
			img->data[i*ch + j] = (unsigned char)jp2->comps[j].data[i];
		}
	}
	return wu_ok;
}

enum wu_error_type jpeg2000_dec(struct image_file *infile) {
	opj_codec_t *dec = opj_create_decompress(OPJ_CODEC_J2K);
	if (!dec) {
		return wu_alloc_error;
	}

	opj_dparameters_t params;
	opj_set_default_decoder_parameters(&params);

	if (!opj_setup_decoder(dec, &params)) {
		opj_destroy_codec(dec);
		return wu_invalid_params;
	}

	opj_image_t *jp2;
	opj_stream_t *stream = opj_stream_create_default_file_stream(
		infile->name, OPJ_TRUE);
	if (!opj_read_header(stream, dec, &jp2)) {
		opj_stream_destroy(stream);
		opj_destroy_codec(dec);
		return wu_invalid_header;
	}

	if (!opj_decode(dec, stream, jp2)) {
		opj_image_destroy(jp2);
		opj_stream_destroy(stream);
		opj_destroy_codec(dec);
		return wu_decoding_error;
	}
	opj_end_decompress(dec, stream);
	opj_stream_destroy(stream);

	struct raw_img *img = alloc_sub_images(infile, 1);
	enum wu_error_type status = join_components(img, jp2);

	opj_image_destroy(jp2);
	opj_destroy_codec(dec);
	return status;
}

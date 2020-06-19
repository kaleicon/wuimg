#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include <webp/decode.h>
#include <webp/demux.h>

#include "wudefs.h"
#include "common.h"
#include "common_composite.h"

struct library_anim {
	WebPAnimDecoder *anim_dec;
	WebPAnimDecoderOptions anim_opts;
};

struct homegrown_anim {
	unsigned char *dec_buf;
	WebPDemuxer *dmux;
	struct anim_frame prev_desc;
	WebPIterator iter;
	WebPMuxAnimDispose dispose;
};

struct webp_state {
	WebPData data;
	WebPDecoderConfig config;

	uint32_t frame_count;
	uint32_t idx;

	enum render_type {
		single,
		homegrown,
		library,
	} anim_render;

	union {
		struct homegrown_anim hanim;
		struct library_anim lanim;
	};
};

static void clean_webp_state(struct image_file *infile, struct webp_state *ds) {
	if (ds->anim_render == homegrown) {
		WebPDemuxReleaseIterator(&ds->hanim.iter);
		WebPDemuxDelete(ds->hanim.dmux);
		free(ds->hanim.dec_buf);
	} else if (ds->anim_render == library) {
		WebPAnimDecoderDelete(ds->lanim.anim_dec);
	}

	WebPFreeDecBuffer(&ds->config.output);
	free((unsigned char *)ds->data.bytes);
	free(ds);
	infile->callback = NULL;
	infile->events = 0;
}

static enum wu_error map_status(VP8StatusCode status, const char **msg) {
	switch (status) {
	case VP8_STATUS_OK:
		*msg = "All OK";
		return wu_ok;
	case VP8_STATUS_OUT_OF_MEMORY:
		*msg = "Out of memory error";
		return wu_alloc_error;
	case VP8_STATUS_INVALID_PARAM:
		*msg = "Invalid parameters";
		return wu_decoding_error;
	case VP8_STATUS_BITSTREAM_ERROR:
		*msg = "Bitstream error";
		return wu_decoding_error;
	case VP8_STATUS_UNSUPPORTED_FEATURE:
		*msg = "Unsupported feature";
		return wu_unsupported_feature;
	case VP8_STATUS_SUSPENDED:
		*msg = "Suspended";
		return wu_unknown_error;
	case VP8_STATUS_USER_ABORT:
		*msg = "User abort";
		return wu_unknown_error;
	case VP8_STATUS_NOT_ENOUGH_DATA:
		*msg = "Not enough data";
		return wu_unexpected_eof;
	default:
		*msg = "No message defined for this error code";
		return wu_unknown_error;
	}
}

static struct anim_frame iter_to_anim_frame(const WebPIterator *iter) {
	const struct anim_frame fr = {
		.x = (size_t)iter->x_offset,
		.y = (size_t)iter->y_offset,
		.w = (size_t)iter->width,
		.h = (size_t)iter->height,
	};
	return fr;
}

static void composite_frame(struct raw_img *img, const unsigned char *dec_buf,
const WebPIterator *iter) {
	const struct anim_frame frame = iter_to_anim_frame(iter);
	if (iter->blend_method == WEBP_MUX_NO_BLEND || !iter->has_alpha) {
		composite_frame_overwrite(img, dec_buf, &frame);
	} else {
		composite_frame_alpha_blend(img, dec_buf, &frame);
	}
}

static enum wu_error libwebp_dec_frame(struct raw_img *img,
const struct wu_conf *wuconf, struct webp_state *ds) {
	struct library_anim *lib = &ds->lanim;

	uint32_t i;
	if (wuconf->keep_frames) {
		i = ds->idx;
	} else {
		i = 0;
	}

	if (!WebPAnimDecoderHasMoreFrames(lib->anim_dec)) {
		WebPAnimDecoderReset(lib->anim_dec);
	}

	unsigned char *buf;
	if (!WebPAnimDecoderGetNext(lib->anim_dec, &buf, &img[i].msec)) {
		return wu_decoding_error;
	}

	if (wuconf->keep_frames) {
		const size_t s = img[i].w * img[i].h * img[i].channels;
		img[i].data = malloc(s);
		if (img[i].data) {
			memcpy(img[i].data, buf, s);
		} else {
			return wu_alloc_error;
		}
	} else {
		img[i].data = buf;
	}

	++ds->idx;
	return wu_ok;
}

static enum wu_error wu_webp_dec_frame(struct raw_img *img,
const struct wu_conf *wuconf, struct webp_state *ds) {
	struct homegrown_anim *hanim = &ds->hanim;

	uint32_t i;
	if (wuconf->keep_frames) {
		i = ds->idx;
	} else {
		i = 0;
	}

	int dec_channels;
	if (img[i].channels == 4 || hanim->iter.has_alpha) {
		dec_channels = 4;
		ds->config.output.colorspace = MODE_RGBA;
	} else {
		dec_channels = 3;
		ds->config.output.colorspace = MODE_RGB;
	}
	const int stride = hanim->iter.width * dec_channels;
	const size_t buf_size = (size_t)(stride * hanim->iter.height);
	ds->config.output.u.RGBA.stride = stride;
	ds->config.output.u.RGBA.size = buf_size;
	VP8StatusCode status = WebPDecode(hanim->iter.fragment.bytes,
		hanim->iter.fragment.size, &ds->config);
	if (status != VP8_STATUS_OK) {
		printf("Failed to decode frame %u\n", i);
		return wu_decoding_error;
	}

	const size_t data_size = img[i].w * img[i].h * img[i].channels;
	if (!img[i].data) {
		img[i].data = malloc(data_size);
		if (!img[i].data) {
			return wu_alloc_error;
		}
	}
	img[i].msec = hanim->iter.duration;

	if (ds->idx == 0) {
		if (img[i].channels == 4) {
			memset(img[i].data, 0, data_size);
		}
		composite_frame(&img[i], hanim->dec_buf, &hanim->iter);
	} else {
		switch (hanim->dispose) {
		case WEBP_MUX_DISPOSE_BACKGROUND:
			if (wuconf->keep_frames) {
				copy_unaffected(&img[i], img[i-1].data,
					&hanim->prev_desc);
			}
			composite_clear(&img[i], &hanim->prev_desc);
			composite_frame(&img[i], hanim->dec_buf, &hanim->iter);
			break;
		case WEBP_MUX_DISPOSE_NONE:
			if (wuconf->keep_frames) {
				memcpy(img[i].data, img[i-1].data, data_size);
			}
			composite_frame(&img[i], hanim->dec_buf, &hanim->iter);
			break;
		}
	}

	hanim->dispose = hanim->iter.dispose_method;
	if (hanim->dispose == WEBP_MUX_DISPOSE_BACKGROUND) {
		hanim->prev_desc = iter_to_anim_frame(&hanim->iter);
	}

	if (!WebPDemuxNextFrame(&hanim->iter)) {
		WebPDemuxGetFrame(hanim->dmux, 1, &hanim->iter);
	} else {
		++ds->idx;
	}
	return wu_ok;
}

static enum wu_error webp_dec_frame(struct raw_img *img,
const struct wu_conf *wuconf, struct webp_state *ds) {
	if (wuconf->webp_use_homegrown_renderer) {
		return wu_webp_dec_frame(img, wuconf, ds);
	} else {
		return libwebp_dec_frame(img, wuconf, ds);
	}
}

static bool webp_next_frame(struct raw_img *img, const struct wu_conf *wuconf,
struct wu_state *state, struct webp_state *ds) {
	const int cycle = state->cycle_sub_img;
	if (cycle > 0) {
		state->cycle_sub_img = 1;
		const enum wu_error err = webp_dec_frame(img, wuconf, ds);
		if (err != wu_ok) {
			return true;
		} else if (ds->idx >= ds->frame_count) {
			if (wuconf->keep_frames) {
				return true;
			} else {
				ds->idx = 0;
			}
		}
	} else if (cycle < 0 && !wuconf->keep_frames) {
		print_temp_line("Cycling back not allowed on "
			"animations.");
		state->cycle_sub_img = 0;
	}
	return false;
}

static enum wu_error webp_callback(struct image_file *infile,
const struct wu_conf *wuconf, struct wu_state *state,
const enum image_event event) {
	bool clean = false;
	struct webp_state *ds = infile->dec_state;
	if (event == sub_cycle) {
		clean = webp_next_frame(infile->sub_img, wuconf, state, ds);
	} else if (event == finish) {
		clean = true;
	}

	if (clean) {
		clean_webp_state(infile, ds);
		if (!wuconf->webp_use_homegrown_renderer
		&& !wuconf->keep_frames) {
			infile->sub_img[0].data = NULL;
			++state->cycle;
		}
	}
	return wu_ok;
}

static int is_covered(const WebPIterator *iter, const struct anim_frame *prev) {
	const int w_diff = iter->width - (int)prev->w;
	const int h_diff = iter->height - (int)prev->h;
	const int x_diff = iter->x_offset - (int)prev->x;
	const int y_diff = iter->y_offset - (int)prev->y;
	return (w_diff - x_diff >= 0) && (h_diff - y_diff >= 0);
}

static unsigned char compute_properties(const WebPDemuxer *dmux,
WebPIterator *iter) {
	unsigned char channels = 3;
	struct anim_frame prev_frame;
	WebPMuxAnimDispose prev_disp = WEBP_MUX_DISPOSE_NONE;
	do {
		/* Reasonably exhaustive tests for alpha. */
		if (iter->has_alpha) {
			if (iter->blend_method == WEBP_MUX_NO_BLEND) {
				channels = 4;
				break;
			}
		}

		if (prev_disp == WEBP_MUX_DISPOSE_BACKGROUND) {
			const int it_is = is_covered(iter, &prev_frame);
			if (iter->has_alpha || !it_is) {
				channels = 4;
				break;
			}
		}

		if (iter->dispose_method == WEBP_MUX_DISPOSE_BACKGROUND) {
			prev_frame = iter_to_anim_frame(iter);
		}
		prev_disp = iter->dispose_method;
	} while (WebPDemuxNextFrame(iter));

	WebPDemuxGetFrame(dmux, 1, iter);
	return channels;
}

static void get_bg_color(const uint32_t color, unsigned char bg[4]) {
	bg[0] = (unsigned char)(color >> 16) & 0xff;
	bg[1] = (unsigned char)(color >> 8) & 0xff;
	bg[2] = (unsigned char)color & 0xff;
	bg[3] = (unsigned char)(color >> 24) & 0xff;
}

static enum wu_error homegrown_anim_setup(struct webp_state *ds,
unsigned char *channels, unsigned char bg_color[4]) {
	struct homegrown_anim *hanim = &ds->hanim;

	hanim->dmux = WebPDemux(&ds->data);
	if (!WebPDemuxGetFrame(hanim->dmux, 1, &hanim->iter)) {
		return wu_decoding_error;
	}

	const size_t canvas_width = (size_t)ds->config.input.width;
	const size_t canvas_height = (size_t)ds->config.input.height;
	// input.has_alpha is true if any frame contains any alpha
	hanim->dec_buf = malloc(canvas_width * canvas_height
		* (size_t)(3 + (ds->config.input.has_alpha == true)) );
	if (!hanim->dec_buf) {
		return wu_alloc_error;
	}

	// However, the composited animation may not need transparency.
	*channels = compute_properties(hanim->dmux, &hanim->iter);

	uint32_t color = WebPDemuxGetI(hanim->dmux, WEBP_FF_BACKGROUND_COLOR);
	get_bg_color(color, bg_color);

	ds->config.output.u.RGBA.rgba = hanim->dec_buf;
	ds->frame_count = WebPDemuxGetI(hanim->dmux, WEBP_FF_FRAME_COUNT);

	hanim->dispose = WEBP_MUX_DISPOSE_NONE;
	return wu_ok;
}

static enum wu_error library_anim_setup(struct webp_state *ds,
unsigned char bg_color[4]) {
	struct library_anim *lanim = &ds->lanim;

	WebPAnimDecoderOptionsInit(&lanim->anim_opts);
	lanim->anim_opts.color_mode = MODE_RGBA;
	lanim->anim_opts.use_threads = 1;

	lanim->anim_dec = WebPAnimDecoderNew(&ds->data, &lanim->anim_opts);
	WebPAnimInfo info;
	WebPAnimDecoderGetInfo(lanim->anim_dec, &info);
	ds->frame_count = info.frame_count;

	get_bg_color(info.bgcolor, bg_color);
	return wu_ok;
}

static enum wu_error anim_setup(const bool homegrown, struct webp_state *ds,
unsigned char *channels, unsigned char bg[4]) {
	if (homegrown) {
		ds->anim_render = homegrown;
		return homegrown_anim_setup(ds, channels, bg);
	} else {
		ds->anim_render = library;
		*channels = 4;
		return library_anim_setup(ds, bg);
	}
}

static VP8StatusCode single_image_decode(struct image_file *infile,
struct webp_state *ds) {
	struct raw_img *img = alloc_sub_images(infile, 1);
	if (!img) {
		return VP8_STATUS_OUT_OF_MEMORY;
	}

	img->w = (size_t)ds->config.input.width;
	img->h = (size_t)ds->config.input.height;
	img->bitdepth = 8;
	if (ds->config.input.has_alpha) {
		img->channels = 4;
		ds->config.output.colorspace = MODE_RGBA;
	} else {
		img->channels = 3;
		ds->config.output.colorspace = MODE_RGB;
	}

	const int stride = (int)(img->w * img->channels);
	const size_t buf_size = (size_t)stride * img->h;
	img->data = malloc(buf_size);
	if (!img->data) {
		return VP8_STATUS_OUT_OF_MEMORY;
	}
	ds->config.output.u.RGBA.rgba = img->data;
	ds->config.output.u.RGBA.stride = stride;
	ds->config.output.u.RGBA.size = buf_size;

	return WebPDecode(ds->data.bytes, ds->data.size, &ds->config);
}

static void set_decoding_options(WebPDecoderConfig *config,
const struct wu_conf *wuconf) {
	config->options.bypass_filtering = wuconf->webp_bypass_filtering;
	config->options.no_fancy_upsampling = wuconf->webp_fast_upsamp;
	config->options.use_threads = true;
	config->output.is_external_memory = true;
}

enum wu_error webp_dec(struct image_file *infile,
const struct wu_conf *wuconf) {
	struct webp_state *ds = calloc(1, sizeof(struct webp_state));
	if (!ds) {
		return wu_alloc_error;
	}

	ds->data.bytes = read_file_to_mem(infile->ifp, &ds->data.size);
	if (!ds->data.bytes) {
		free(ds);
		return wu_open_error;
	}

	WebPInitDecoderConfig(&ds->config);
	VP8StatusCode status = WebPGetFeatures(ds->data.bytes, ds->data.size,
		&ds->config.input);
	if (status != VP8_STATUS_OK) {
		clean_webp_state(infile, ds);
		return wu_invalid_header;
	}

	const unsigned int width = (unsigned int)ds->config.input.width;
	const unsigned int height = (unsigned int)ds->config.input.height;
	if (umax(width, height) > wuconf->max_img_size) {
		clean_webp_state(infile, ds);
		return wu_exceeded_size_limit;
	}

	set_decoding_options(&ds->config, wuconf);

	enum wu_error err = wu_ok;
	if (ds->config.input.has_animation) {
		unsigned char channels;
		err = anim_setup(wuconf->webp_use_homegrown_renderer, ds,
			&channels, infile->bg);
		if (err != wu_ok) {
			clean_webp_state(infile, ds);
			return err;
		}

		struct raw_img *img;
		if (wuconf->keep_frames) {
			img = alloc_sub_images(infile, ds->frame_count);
		} else {
			img = alloc_sub_images(infile, 1);
		}
		if (!img) {
			clean_webp_state(infile, ds);
			return wu_alloc_error;
		}

		for (uint32_t i = 0; i < infile->nr; ++i) {
			img[i].w = width;
			img[i].h = height;
			img[i].channels = channels;
			img[i].bitdepth = 8;
		}

		webp_dec_frame(img, wuconf, ds);

		if (ds->frame_count > 1) {
			infile->is_animation = true;
			infile->callback = webp_callback;
			infile->dec_state = ds;
			infile->events = sub_cycle;
		} else {
			if (!wuconf->webp_use_homegrown_renderer) {
				const size_t s = img[0].w * img[0].h * channels;
				unsigned char *cpy = malloc(s);
				if (cpy) {
					memcpy(cpy, img[0].data, s);
					img[0].data = cpy;
				} else {
					img[0].data = NULL;
					clean_webp_state(infile, ds);
					return wu_alloc_error;
				}
			}
			clean_webp_state(infile, ds);
		}
	} else {
		status = single_image_decode(infile, ds);
		clean_webp_state(infile, ds);
	}

	if (status != VP8_STATUS_OK) {
		const char *msg;
		err = map_status(status, &msg);
		infile->err_msg = strdup(msg);
	}
	return err;
}

bool webp_verify(FILE *ifp) {
	/* WebP header is "RIFF<le-u32>WEBP" where <le-u32> is the file size
	 * starting from "WEBP" */
	const unsigned char more_magic[] = {'W', 'E', 'B', 'P'};

	const size_t sig_len = 8;
	fseek(ifp, 4, SEEK_SET);
	unsigned char signature[sig_len];
	const size_t read = fread(signature, 1, sig_len, ifp);

	if (read == sig_len
	&& !memcmp(signature + 4, more_magic, sizeof(more_magic))) {
		fseek(ifp, 0, SEEK_END);
		const size_t size = (size_t)ftell(ifp) - 8;
		const size_t expected_size = endian_u32(signature,
			little_endian);
		if (expected_size == size) {
			return true;
		} else {
			printf("Expected size %zu, got %zu\n", expected_size,
				size);
		}
	}
	return false;
}

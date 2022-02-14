#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include <webp/decode.h>
#include <webp/demux.h>

#include "../wudefs.h"
#include "../common.h"
#include "../metadata.h"
#include "../raster/compost.h"

struct frame_dispose {
	WebPMuxAnimDispose method;
	struct frame_info *bg_geom;
};

struct library_anim {
	WebPAnimDecoder *dec;
	int prev_msec;
};

struct homegrown_anim {
	unsigned char *dec_buf;
	WebPDemuxer *dmux;
	WebPIterator iter;
	struct frame_dispose dispose;
};

struct webp_state {
	struct map_info map;
	WebPData data;
	WebPDecoderConfig config;

	int idx;

	enum render_type {
		webp_single,
		webp_homegrown,
		webp_library,
	} anim_render;

	union {
		struct homegrown_anim h;
		struct library_anim l;
	} anim;
};

static void clean_webp_state(struct image_file *infile) {
	struct webp_state *ds = infile->dec_state;
	switch (ds->anim_render) {
	case webp_homegrown:
		WebPDemuxReleaseIterator(&ds->anim.h.iter);
		WebPDemuxDelete(ds->anim.h.dmux);
		free(ds->anim.h.dec_buf);
		break;
	case webp_library:
		WebPAnimDecoderDelete(ds->anim.l.dec);
		break;
	case webp_single: break;
	}

	WebPFreeDecBuffer(&ds->config.output);
	unmap_file(&ds->map);
	free(ds);
	infile->dec_state = NULL;
	infile->events = 0;
}

static void rewind_webp_state(struct webp_state *ds) {
	ds->idx = 0;
	if (ds->anim_render == webp_library) {
		WebPAnimDecoderReset(ds->anim.l.dec);
	}
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
	}
	*msg = "No message defined for this error code";
	return wu_unknown_error;
}

static void compost_frame(struct raw_img *img, struct homegrown_anim *hanim,
struct frame_info *frame) {
	if (hanim->iter.blend_method == WEBP_MUX_NO_BLEND || !hanim->iter.has_alpha) {
		compost_overwrite(img->data, img->w, img->channels,
			hanim->dec_buf, frame);
	} else {
		compost_alpha_blend(img->data, img->w, img->channels,
			hanim->dec_buf, frame);
	}
}

static enum wu_error libwebp_dec_frame(struct raw_img *img,
struct webp_state *ds) {
	unsigned char *buf;
	int msec;
	if (!WebPAnimDecoderGetNext(ds->anim.l.dec, &buf, &msec)) {
		return wu_decoding_error;
	}

	if (ds->idx == 0) {
		img->frames->f[ds->idx].msec = msec;
	} else {
		img->frames->f[ds->idx].msec = msec - ds->anim.l.prev_msec;
	}
	ds->anim.l.prev_msec = msec;
	img->data = buf;

	++ds->idx;
	return wu_ok;
}

static enum wu_error homegrown_dec_frame(struct raw_img *img,
struct webp_state *ds) {
	struct homegrown_anim *hanim = &ds->anim.h;
	WebPDemuxGetFrame(hanim->dmux, ds->idx + 1, &hanim->iter);

	int dec_channels;
	if (img->channels == 4 || hanim->iter.has_alpha) {
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
		return wu_decoding_error;
	}

	const size_t data_size = raw_img_size(img);
	if (!img->data) {
		img->data = malloc(data_size);
		if (!img->data) {
			return wu_alloc_error;
		}
	}

	if (ds->idx == 0) {
		if (img->channels == 4) {
			memset(img->data, 0, data_size);
		}
	} else {
		switch (hanim->dispose.method) {
		case WEBP_MUX_DISPOSE_BACKGROUND:
			compost_clear(img->data, img->w, img->channels, 0,
				hanim->dispose.bg_geom);
			break;
		case WEBP_MUX_DISPOSE_NONE:
			break;
		}
	}
	struct frame_info *frame = img->frames->f + ds->idx;
	compost_frame(img, hanim, frame);

	hanim->dispose.method = hanim->iter.dispose_method;
	if (hanim->dispose.method == WEBP_MUX_DISPOSE_BACKGROUND) {
		hanim->dispose.bg_geom = frame;
	}

	++ds->idx;
	return wu_ok;
}

static enum wu_error webp_dec_frame(struct raw_img *img,
struct webp_state *ds) {
	if (ds->anim_render == webp_homegrown) {
		return homegrown_dec_frame(img, ds);
	}
	return libwebp_dec_frame(img, ds);
}

static enum wu_error webp_frame_iter(struct image_file *infile,
struct wu_state *state) {
	struct webp_state *ds = infile->dec_state;

	if (ds->idx > state->frame) {
		rewind_webp_state(ds);
	}
	while (ds->idx <= state->frame) {
		const enum wu_error err = webp_dec_frame(infile->sub_img, ds);
		if (err != wu_ok) {
			return err;
		}
	}
	return wu_ok;
}

enum wu_error webp_callback(struct image_file *infile,
const struct wu_conf *wuconf, struct wu_state *state,
const enum image_event event) {
	(void)wuconf;
	bool clean = false;
	enum wu_error status = wu_ok;
	if (event == ev_frame) {
		status = webp_frame_iter(infile, state);
		clean = (status != wu_ok);
	} else if (event == 0) {
		clean = true;
	}

	if (clean) {
		struct webp_state *ds = infile->dec_state;
		if (ds->anim_render == webp_library) {
			infile->sub_img->data = NULL;
		}
		clean_webp_state(infile);
	}
	return status;
}

static bool is_covered(const WebPIterator *iter, const struct frame_info *geom) {
	const int w_diff = iter->width - (int)geom->w;
	const int h_diff = iter->height - (int)geom->h;
	const int x_diff = iter->x_offset - (int)geom->x;
	const int y_diff = iter->y_offset - (int)geom->y;
	return (w_diff - x_diff >= 0) && (h_diff - y_diff >= 0);
}

static struct frame_info iter_to_anim_frame(const WebPIterator *iter) {
	return (struct frame_info) {
		.x = (size_t)iter->x_offset,
		.y = (size_t)iter->y_offset,
		.w = (size_t)iter->width,
		.h = (size_t)iter->height,
		.msec = iter->duration,
	};
}

static enum wu_error gather_info(struct raw_img *img, WebPIterator *iter) {
	struct image_frames *frames = raw_img_alloc_frames(img,
		(size_t)iter->num_frames);
	if (!frames) {
		return wu_alloc_error;
	}

	struct frame_info *f = img->frames->f;
	struct frame_dispose disp = {.method = WEBP_MUX_DISPOSE_NONE};
	int i = 0;
	do {
		f[i] = iter_to_anim_frame(iter);
		if (img->channels == 3) {
			/* Reasonably exhaustive tests for alpha. */
			if (iter->has_alpha) {
				if (iter->blend_method == WEBP_MUX_NO_BLEND) {
					img->channels = 4;
				}
			}

			if (disp.method == WEBP_MUX_DISPOSE_BACKGROUND) {
				const bool covered = is_covered(iter, disp.bg_geom);
				if (iter->has_alpha || !covered) {
					img->channels = 4;
				}
			}
			disp.method = iter->dispose_method;
			if (iter->dispose_method == WEBP_MUX_DISPOSE_BACKGROUND) {
				disp.bg_geom = f + i;
			}
		}
		++i;
	} while (WebPDemuxNextFrame(iter));
	return wu_ok;
}

static struct pix_rgba8 get_bg_color(const uint32_t color) {
	// BGRA byte order
	return (struct pix_rgba8) {
		.r = (unsigned char)(color >> 16),
		.g = (unsigned char)(color >> 8),
		.b = (unsigned char)color,
		.a = (unsigned char)(color >> 24),
	};
}

static enum wu_error homegrown_anim_setup(struct raw_img *img,
struct webp_state *ds, struct pix_rgba8 *bg_color) {
	struct homegrown_anim *hanim = &ds->anim.h;

	if (!WebPDemuxGetFrame(hanim->dmux, 1, &hanim->iter)) {
		return wu_decoding_error;
	}

	const size_t canvas_width = (size_t)ds->config.input.width;
	const size_t canvas_height = (size_t)ds->config.input.height;
	hanim->dec_buf = malloc(canvas_width * canvas_height * 4);
	if (!hanim->dec_buf) {
		return wu_alloc_error;
	}

	*bg_color = get_bg_color(
		WebPDemuxGetI(hanim->dmux, WEBP_FF_BACKGROUND_COLOR));

	ds->config.output.u.RGBA.rgba = hanim->dec_buf;

	hanim->dispose.method = WEBP_MUX_DISPOSE_NONE;
	return gather_info(img, &hanim->iter);
}

static enum wu_error library_anim_setup(struct raw_img *img,
struct webp_state *ds, struct pix_rgba8 *bg_color) {
	WebPAnimDecoderOptions anim_opts;
	WebPAnimDecoderOptionsInit(&anim_opts);
	anim_opts.color_mode = MODE_RGBA;
	anim_opts.use_threads = true;

	ds->anim.l.dec = WebPAnimDecoderNew(&ds->data, &anim_opts);
	WebPAnimInfo info;
	WebPAnimDecoderGetInfo(ds->anim.l.dec, &info);
	*bg_color = get_bg_color(info.bgcolor);

	const WebPDemuxer *dmux = WebPAnimDecoderGetDemuxer(ds->anim.l.dec);
	WebPIterator iter;
	WebPDemuxGetFrame(dmux, 1, &iter);
	return gather_info(img, &iter);
}

static enum wu_error anim_setup(struct raw_img *img, struct webp_state *ds,
struct pix_rgba8 *bg) {
	if (ds->anim_render == webp_homegrown) {
		return homegrown_anim_setup(img, ds, bg);
	} else {
		img->channels = 4;
		return library_anim_setup(img, ds, bg);
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

	const bool alpha = ds->config.input.has_alpha;
	img->channels = alpha ? 4 : 3;
	img->disable_alpha = !alpha;
	WEBP_CSP_MODE colorspace;
	if (ds->config.input.format == 1) {
		struct image_planes *planes = raw_img_plane_init(img);
		if (!planes) {
			return VP8_STATUS_OUT_OF_MEMORY;
		}

		planes->cs = color_space_ycbcr_limited;
		raw_img_plane_subsamp(img, pix_yuv420);
		if (!raw_img_plane_alloc(img)) {
			return VP8_STATUS_OUT_OF_MEMORY;
		}

		struct plane_info *p = planes->p;
		ds->config.output.u.YUVA = (struct WebPYUVABuffer) {
			.y = p[0].ptr,
			.u = p[1].ptr,
			.v = p[2].ptr,
			.a = p[3].ptr,
			.y_stride = (int)p[0].stride,
			.u_stride = (int)p[1].stride,
			.v_stride = (int)p[2].stride,
			.a_stride = (int)p[3].stride,
			.y_size = p[0].size,
			.u_size = p[1].size,
			.v_size = p[2].size,
			.a_size = p[3].size,
		};
		colorspace = alpha ? MODE_YUVA : MODE_YUV;
	} else {
		const size_t stride = raw_img_addbuf(img);
		if (!stride) {
			return VP8_STATUS_OUT_OF_MEMORY;
		}
		const size_t buf_size = stride * img->h;

		ds->config.output.u.RGBA = (struct WebPRGBABuffer) {
			.rgba = img->data,
			.stride = (int)stride,
			.size = buf_size,
		};
		colorspace = alpha ? MODE_RGBA : MODE_RGB;
	}
	ds->config.output.colorspace = colorspace;
	return WebPDecode(ds->data.bytes, ds->data.size, &ds->config);
}

static void set_decoding_options(WebPDecoderConfig *config,
const struct wu_conf *wuconf) {
	config->options.bypass_filtering = wuconf->webp_bypass_filtering;
	config->options.no_fancy_upsampling = wuconf->webp_fast_upsamp;
	config->options.use_threads = true;
	config->output.is_external_memory = true;
}

static void loop_over_chunks(struct wu_tree *tree, WebPDemuxer *dmux,
WebPChunkIterator *chunks, const char *fourcc, const enum metadata_type type,
const size_t offset) {
	if (WebPDemuxGetChunk(dmux, fourcc, 1, chunks)) {
		do {
			if (chunks->chunk.size > offset) {
				standard_metadata(type,
					chunks->chunk.bytes + offset,
					chunks->chunk.size - offset, tree);
			}
		} while (WebPDemuxNextChunk(chunks));
	}
}

static void read_metadata(struct wu_tree *tree, struct webp_state *ds,
bool use_homegrown) {
	WebPDemuxer *dmux = WebPDemux(&ds->data);
	const uint32_t flags = WebPDemuxGetI(dmux, WEBP_FF_FORMAT_FLAGS);

	WebPChunkIterator chunks;
	if (flags & EXIF_FLAG) {
		loop_over_chunks(tree, dmux, &chunks, "EXIF", exif_metadata, 6);
	}
	if (flags & XMP_FLAG) {
		loop_over_chunks(tree, dmux, &chunks, "XMP ", xmp_metadata, 0);
	}

	if (ds->config.input.has_animation && use_homegrown) {
		ds->anim.h.dmux = dmux;
	} else {
		WebPDemuxDelete(dmux);
	}

	const char *fmt = NULL;
	switch (ds->config.input.format) {
	case 0: fmt = "Mixed"; break;
	case 1: fmt = "Lossy"; break;
	case 2: fmt = "Lossless"; break;
	default: return;
	}
	tree_sprout_leaf(tree, "Compression", fmt);
}

enum wu_error webp_dec(struct image_file *infile,
const struct wu_conf *wuconf) {
	struct webp_state *ds = calloc(1, sizeof(*ds));
	if (!ds) {
		return wu_alloc_error;
	}

	if (!map_file(&ds->map, infile->ifp)) {
		free(ds);
		return wu_alloc_error;
	}

	ds->data.size = ds->map.len;
	ds->data.bytes = ds->map.data;
	infile->dec_state = ds;

	WebPInitDecoderConfig(&ds->config);
	VP8StatusCode status = WebPGetFeatures(ds->data.bytes, ds->data.size,
		&ds->config.input);
	if (status != VP8_STATUS_OK) {
		clean_webp_state(infile);
		return wu_invalid_header;
	}

	read_metadata(&infile->metadata, ds,
		wuconf->webp_use_homegrown_renderer);

	const unsigned int width = (unsigned int)ds->config.input.width;
	const unsigned int height = (unsigned int)ds->config.input.height;
	if (umax(width, height) > wuconf->max_img_size) {
		clean_webp_state(infile);
		return wu_exceeds_size_limit;
	}

	set_decoding_options(&ds->config, wuconf);

	enum wu_error err = wu_ok;
	if (ds->config.input.has_animation) {
		struct raw_img *img = alloc_sub_images(infile, 1);
		if (!img) {
			clean_webp_state(infile);
			return wu_alloc_error;
		}

		img->w = width;
		img->h = height;
		img->channels = wuconf->anim_space_over_speed ? 3 : 4;
		img->bitdepth = 8;

		ds->anim_render = wuconf->webp_use_homegrown_renderer
			? webp_homegrown : webp_library;
		err = anim_setup(img, ds, &infile->bg);
		if (err != wu_ok) {
			clean_webp_state(infile);
			return err;
		}

		webp_dec_frame(img, ds);

		if (img->frames->nr > 1) {
			infile->dec_state = ds;
			infile->events = ev_frame;
		} else { // I don't think this case is possible
			if (ds->anim_render == webp_library) {
				// Memory is not ours
				const size_t s = img->w * img->h
					* img->channels;
				unsigned char *cpy = memdup(img->data, s);
				if (cpy) {
					img->data = cpy;
				} else {
					img->data = NULL;
					err = wu_alloc_error;
				}
			}
			free(img->frames);
			img->frames = NULL;
			clean_webp_state(infile);
		}
	} else {
		status = single_image_decode(infile, ds);
		clean_webp_state(infile);
	}

	if (status != VP8_STATUS_OK) {
		const char *msg;
		err = map_status(status, &msg);
		image_file_error_append(infile, msg);
	}
	return err;
}

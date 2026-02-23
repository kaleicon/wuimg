// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2019 kaleido
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include <webp/decode.h>
#include <webp/demux.h>

#include "wudefs.h"
#include "misc/metadata.h"
#include "raster/compost.h"

struct frame_dispose {
	WebPMuxAnimDispose method;
	const struct compost *bg_geom;
};

struct homegrown_anim {
	struct wustr dec_buf;
	WebPDemuxer *dmux;
	WebPIterator iter;
	struct frame_dispose dispose;
};

struct webp_state {
	WebPData data;
	WebPDecoderConfig config;

	enum render_type {
		webp_single = 0,
		webp_homegrown,
		webp_library,
	} anim_render;

	union {
		struct homegrown_anim h;
		WebPAnimDecoder *dec;
	} anim;
};

static void end_webp(struct image_file *infile) {
	struct webp_state *ds = infile->dec_state;
	switch (ds->anim_render) {
	case webp_homegrown:
		WebPDemuxReleaseIterator(&ds->anim.h.iter);
		WebPDemuxDelete(ds->anim.h.dmux);
		wustr_free(&ds->anim.h.dec_buf);
		break;
	case webp_library:
		WebPAnimDecoderDelete(ds->anim.dec);
		break;
	case webp_single: break;
	}

	WebPFreeDecBuffer(&ds->config.output);
}

static int rewind_webp_state(struct webp_state *ds, struct wuimg *img,
const int current, const int frame) {
	if (ds->anim_render == webp_library) {
		if (frame < current) {
			WebPAnimDecoderReset(ds->anim.dec);
			return 0;
		}
		return current + 1;
	}
	return wuimg_frame_prev_nearest(img, current, frame);
}

static struct wu_st map_webp_status(VP8StatusCode status) {
	switch (status) {
	case VP8_STATUS_OK:
		return WU_OK;
	case VP8_STATUS_OUT_OF_MEMORY:
		return wuerr(wu_alloc_error, NULL);
	case VP8_STATUS_INVALID_PARAM:
		return wuerr(wu_invalid_params, NULL);
	case VP8_STATUS_BITSTREAM_ERROR:
		return wuerr(wu_decoding_error, "bitstream error");
	case VP8_STATUS_UNSUPPORTED_FEATURE:
		return wuerr(wu_unsupported_feature, NULL);
	case VP8_STATUS_SUSPENDED:
		return wuerr(wu_unknown_error, "VP8 suspended");
	case VP8_STATUS_USER_ABORT:
		return wuerr(wu_unknown_error, "user abort");
	case VP8_STATUS_NOT_ENOUGH_DATA:
		return wuerr(wu_unexpected_eof, NULL);
	}
	return wuerr(wu_unknown_error, "No message defined for this error code");
}

static void compost_webp_frame(struct wuimg *img, struct homegrown_anim *hanim,
const struct compost *reg) {
	if (hanim->iter.blend_method == WEBP_MUX_NO_BLEND || !hanim->iter.has_alpha) {
		compost_overwrite(img->data, img->w, img->channels,
			hanim->dec_buf.str, reg);
	} else {
		compost_alpha_blend(img->data, img->w, //img->channels,
			hanim->dec_buf.str, reg);
	}
}

static struct wu_st libwebp_dec_frame(struct wuimg *img,
struct webp_state *ds) {
	int msec;
	return WebPAnimDecoderGetNext(ds->anim.dec, (uint8_t **)&img->data, &msec)
		? WU_OK : WUERR_HERE(wu_decoding_error);
}

static struct wu_st homegrown_dec_frame(struct wuimg *img,
struct webp_state *ds, const int idx) {
	struct homegrown_anim *hanim = &ds->anim.h;
	WebPDemuxGetFrame(hanim->dmux, idx + 1, &hanim->iter);

	const struct frame_info *frame = img->frames->f + idx;
	const int stride = hanim->iter.width * img->channels;
	const size_t buf_size = (size_t)(stride * hanim->iter.height);
	ds->config.output.colorspace = MODE_BGRA;
	ds->config.output.u.RGBA.stride = stride;
	ds->config.output.u.RGBA.size = buf_size;
	if (frame->keyframe) {
		ds->config.output.u.RGBA.rgba = img->data;
	} else {
		if (hanim->dec_buf.len < buf_size) {
			if (!wustr_realloc(&hanim->dec_buf, buf_size)) {
				return WUERR_HERE(wu_alloc_error);
			}
		}
		ds->config.output.u.RGBA.rgba = hanim->dec_buf.str;
	}

	VP8StatusCode status = WebPDecode(hanim->iter.fragment.bytes,
		hanim->iter.fragment.size, &ds->config);
	if (status != VP8_STATUS_OK) {
		return map_webp_status(status);
	}

	if (!frame->keyframe) {
		if (idx == 0) {
			memset(img->data, 0, wuimg_size(img));
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
		compost_webp_frame(img, hanim, &frame->reg);

		hanim->dispose.method = hanim->iter.dispose_method;
		if (hanim->dispose.method == WEBP_MUX_DISPOSE_BACKGROUND) {
			hanim->dispose.bg_geom = &frame->reg;
		}
	}
	return WU_OK;
}

static struct wu_st dec_webp_frame(struct wuimg *img,
struct webp_state *ds, const int idx) {
	if (ds->anim_render == webp_homegrown) {
		return homegrown_dec_frame(img, ds, idx);
	}
	return libwebp_dec_frame(img, ds);
}

static struct wu_st event_webp(struct image_file *infile,
struct wu_state *state, const enum image_event event) {
	if (event != ev_frame) {
		return WU_NO_CHANGE;
	}

	struct webp_state *ds = infile->dec_state;
	struct wuimg *img = infile->sub_img;
	int idx = rewind_webp_state(ds, img, img->frames->current, state->frame);
	while (idx <= state->frame) {
		const struct wu_st st = dec_webp_frame(img, ds, idx);
		++idx;
		if (!wu_isok(st)) {
			return st;
		}
	}
	img->frames->current = state->frame;
	return WU_OK;
}

static struct wu_st gather_webp_info(struct wuimg *img, WebPIterator *iter) {
	if (!wuimg_frames_init(img, (size_t)iter->num_frames)) {
		return WUERR_HERE(wu_alloc_error);
	}

	size_t i = 0;
	do {
		const bool valid = wuimg_frame_set(img, i,
			(size_t)iter->x_offset, (size_t)iter->y_offset,
			(size_t)iter->width, (size_t)iter->height,
			(uint32_t)iter->duration, 1000,
			iter->blend_method == WEBP_MUX_NO_BLEND);
		if (!valid) {
			return WUERR_HERE(wu_alloc_error);
		}
		++i;
	} while (WebPDemuxNextFrame(iter));
	return WU_OK;
}

static struct pix_rgba8 get_webp_bg_color(const uint32_t color) {
	// BGRA byte order
	return (struct pix_rgba8) {
		.r = (unsigned char)(color >> 16),
		.g = (unsigned char)(color >> 8),
		.b = (unsigned char)color,
		.a = (unsigned char)(color >> 24),
	};
}

static struct wu_st homegrown_anim_setup(struct wuimg *img,
struct webp_state *ds, const struct wu_conf *conf, struct pix_rgba8 *bg_color) {
	ds->anim_render = webp_homegrown;
	struct homegrown_anim *hanim = &ds->anim.h;
	if (!WebPDemuxGetFrame(hanim->dmux, 1, &hanim->iter)) {
		return WUERR_HERE(wu_decoding_error);
	}

	const enum wu_error err = wuimg_alloc_limit(img, conf);
	if (err != wu_ok) {
		return WUERR_HERE(err);
	}
	*bg_color = get_webp_bg_color(
		WebPDemuxGetI(hanim->dmux, WEBP_FF_BACKGROUND_COLOR));
	hanim->dispose.method = WEBP_MUX_DISPOSE_NONE;
	return gather_webp_info(img, &hanim->iter);
}

static struct wu_st library_anim_setup(struct wuimg *img,
struct webp_state *ds, const struct wu_conf *conf, struct pix_rgba8 *bg_color) {
	ds->anim_render = webp_library;
	if (wuimg_exceeds_limit(img, conf)) {
		return WUERR_HERE(wu_exceeds_size_limit);
	}
	const struct wu_st st = wuimg_verify_st(img);
	if (!wu_isok(st)) {
		return st;
	}
	WebPAnimDecoderOptions anim_opts;
	WebPAnimDecoderOptionsInit(&anim_opts);
	anim_opts.color_mode = MODE_BGRA;
	anim_opts.use_threads = true;

	ds->anim.dec = WebPAnimDecoderNew(&ds->data, &anim_opts);
	WebPAnimInfo info;
	WebPAnimDecoderGetInfo(ds->anim.dec, &info);
	*bg_color = get_webp_bg_color(info.bgcolor);

	const WebPDemuxer *dmux = WebPAnimDecoderGetDemuxer(ds->anim.dec);
	WebPIterator iter;
	WebPDemuxGetFrame(dmux, 1, &iter);
	img->borrowed = true;
	return gather_webp_info(img, &iter);
}

static struct wu_st setup_webp_anim(struct wuimg *img, struct webp_state *ds,
const struct wu_conf *conf, struct pix_rgba8 *bg) {
	if (conf->webp_use_homegrown_renderer) {
		return homegrown_anim_setup(img, ds, conf, bg);
	}
	return library_anim_setup(img, ds, conf, bg);
}

static struct wu_st single_image_decode(struct wuimg *img,
struct webp_state *ds, const struct wu_conf *conf) {
	const bool alpha = ds->config.input.has_alpha;
	img->channels = alpha ? 4 : 3;
	WEBP_CSP_MODE colorspace;
	if (ds->config.input.format == 1) {
		struct image_planes *planes = wuimg_plane_init(img);
		if (!planes) {
			return WUERR_HERE(wu_alloc_error);
		}

		wuimg_plane_subsamp(img, 2, 2);
		img->cs.matrix = cicp_matrix_bt601_7;
		img->cs.limited = true;

		enum wu_error err = wuimg_alloc_limit(img, conf);
		if (err != wu_ok) {
			return WUERR_HERE(err);
		}
		struct plane_info *p = planes->p;
		ds->config.output.u.YUVA = (struct WebPYUVABuffer) {
			.y = p[0].ptr,
			.u = p[1].ptr,
			.v = p[2].ptr,
			.a = alpha ? p[3].ptr : NULL,
			.y_stride = (int)p[0].stride,
			.u_stride = (int)p[1].stride,
			.v_stride = (int)p[2].stride,
			.a_stride = alpha ? (int)p[3].stride : 0,
			.y_size = p[0].size,
			.u_size = p[1].size,
			.v_size = p[2].size,
			.a_size = alpha ? p[3].size : 0,
		};
		colorspace = alpha ? MODE_YUVA : MODE_YUV;
	} else {
		enum wu_error err = wuimg_alloc_limit(img, conf);
		if (err != wu_ok) {
			return WUERR_HERE(err);
		}
		const size_t stride = wuimg_stride(img);

		ds->config.output.u.RGBA = (struct WebPRGBABuffer) {
			.rgba = img->data,
			.stride = (int)stride,
			.size = stride * img->h,
		};
		colorspace = alpha ? MODE_BGRA : MODE_BGR;
		img->layout = pix_bgra;
	}
	ds->config.output.colorspace = colorspace;
	return map_webp_status(WebPDecode(ds->data.bytes, ds->data.size, &ds->config));
}

static void loop_over_chunks(struct wutree *tree, WebPDemuxer *dmux,
WebPChunkIterator *chunks, const char *fourcc, const enum metadata_type type,
const size_t offset) {
	if (WebPDemuxGetChunk(dmux, fourcc, 1, chunks)) {
		do {
			if (chunks->chunk.size > offset) {
				metadata_parse(type,
					chunks->chunk.bytes + offset,
					chunks->chunk.size - offset, tree);
			}
		} while (WebPDemuxNextChunk(chunks));
	}
}

static void read_webp_metadata(struct wutree *tree, struct webp_state *ds,
bool use_homegrown) {
	WebPDemuxer *dmux = WebPDemux(&ds->data);
	const uint32_t flags = WebPDemuxGetI(dmux, WEBP_FF_FORMAT_FLAGS);

	WebPChunkIterator chunks;
	if (flags & EXIF_FLAG) {
		loop_over_chunks(tree, dmux, &chunks, "EXIF", metadata_exif, 6);
	}
	if (flags & XMP_FLAG) {
		loop_over_chunks(tree, dmux, &chunks, "XMP ", metadata_xmp, 0);
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
	tree_add_leaf_utf8(tree, "Compression", fmt);
}

static struct wu_st init_webp(struct image_file *infile) {
	struct webp_state *ds = infile->dec_state;
	ds->data = (WebPData) {
		.size = infile->map.len,
		.bytes = infile->map.ptr,
	};

	WebPInitDecoderConfig(&ds->config);
	VP8StatusCode status = WebPGetFeatures(ds->data.bytes, ds->data.size,
		&ds->config.input);
	if (status != VP8_STATUS_OK) {
		return map_webp_status(status);
	}

	const struct wu_conf *conf = infile->conf;
	read_webp_metadata(&infile->metadata, ds,
		conf->webp_use_homegrown_renderer);

	struct wuimg *img = infile->sub_img;
	img->w = (size_t)ds->config.input.width;
	img->h = (size_t)ds->config.input.height;
	img->bitdepth = 8;

	ds->config.options.bypass_filtering = conf->webp_bypass_filtering;
	ds->config.options.no_fancy_upsampling = conf->webp_fast_upsamp;
	ds->config.options.use_threads = true;
	ds->config.output.is_external_memory = true;

	struct wu_st st = WU_OK;
	if (ds->config.input.has_animation) {
		img->channels = 4;
		img->layout = pix_bgra;

		st = setup_webp_anim(img, ds, conf, &infile->bg);
		if (wu_isok(st)) {
			st = dec_webp_frame(img, ds, 0);
		}
	} else {
		st = single_image_decode(img, ds, conf);
	}
	return st;
}

const struct image_fn webp_fn = {
	.mmap = true,
	.alloc_single = true,
	.state_size = sizeof(struct webp_state),
	.init = init_webp,
	.event = event_webp,
	.end = end_webp,
};

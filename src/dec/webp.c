// SPDX-License-Identifier: 0BSD
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include <webp/decode.h>
#include <webp/demux.h>

#include "wudefs.h"
#include "metadata.h"
#include "misc/file.h"
#include "raster/compost.h"

struct frame_dispose {
	WebPMuxAnimDispose method;
	const struct frame_info *bg_geom;
};

struct library_anim {
	WebPAnimDecoder *dec;
	int prev_msec;
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

	int idx;

	enum render_type {
		webp_single = 0,
		webp_homegrown,
		webp_library,
	} anim_render;

	union {
		struct homegrown_anim h;
		struct library_anim l;
	} anim;
};

static void webp_end(struct image_file *infile) {
	struct webp_state *ds = infile->dec_state;
	switch (ds->anim_render) {
	case webp_homegrown:
		WebPDemuxReleaseIterator(&ds->anim.h.iter);
		WebPDemuxDelete(ds->anim.h.dmux);
		wustr_free(&ds->anim.h.dec_buf);
		break;
	case webp_library:
		WebPAnimDecoderDelete(ds->anim.l.dec);
		infile->sub_img->data = NULL;
		break;
	case webp_single: break;
	}

	WebPFreeDecBuffer(&ds->config.output);
}

static void rewind_webp_state(struct webp_state *ds, struct wuimg *img,
const int frame) {
	if (ds->anim_render == webp_library) {
		WebPAnimDecoderReset(ds->anim.l.dec);
		ds->idx = 0;
		ds->anim.l.prev_msec = 0;
	} else {
		ds->idx = wuimg_frame_prev_keyframe(img, ds->idx, frame);
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
		return wu_invalid_params;
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

static void compost_frame(struct wuimg *img, struct homegrown_anim *hanim,
const struct frame_info *frame) {
	if (hanim->iter.blend_method == WEBP_MUX_NO_BLEND || !hanim->iter.has_alpha) {
		compost_overwrite(img->data, img->w, img->channels,
			hanim->dec_buf.str, frame);
	} else {
		compost_alpha_blend(img->data, img->w, //img->channels,
			hanim->dec_buf.str, frame);
	}
}

static enum wu_error libwebp_dec_frame(struct wuimg *img,
struct webp_state *ds) {
	unsigned char *buf;
	int msec;
	if (!WebPAnimDecoderGetNext(ds->anim.l.dec, &buf, &msec)) {
		return wu_decoding_error;
	}

	img->frames->f[ds->idx].sec = (float)(msec - ds->anim.l.prev_msec) / 1000;
	ds->anim.l.prev_msec = msec;
	img->data = buf;
	++ds->idx;
	return wu_ok;
}

static enum wu_error homegrown_dec_frame(struct wuimg *img,
struct webp_state *ds) {
	struct homegrown_anim *hanim = &ds->anim.h;
	WebPDemuxGetFrame(hanim->dmux, ds->idx + 1, &hanim->iter);

	const struct frame_info *frame = img->frames->f + ds->idx;
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
				return wu_alloc_error;
			}
		}
		ds->config.output.u.RGBA.rgba = hanim->dec_buf.str;
	}

	VP8StatusCode status = WebPDecode(hanim->iter.fragment.bytes,
		hanim->iter.fragment.size, &ds->config);
	if (status != VP8_STATUS_OK) {
		return wu_decoding_error;
	}

	if (!frame->keyframe) {
		if (ds->idx == 0) {
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
		compost_frame(img, hanim, frame);

		hanim->dispose.method = hanim->iter.dispose_method;
		if (hanim->dispose.method == WEBP_MUX_DISPOSE_BACKGROUND) {
			hanim->dispose.bg_geom = frame;
		}
	}
	++ds->idx;
	return wu_ok;
}

static enum wu_error webp_dec_frame(struct wuimg *img,
struct webp_state *ds) {
	if (ds->anim_render == webp_homegrown) {
		return homegrown_dec_frame(img, ds);
	}
	return libwebp_dec_frame(img, ds);
}

static enum wu_error webp_frame_iter(struct image_file *infile,
struct wu_state *state) {
	struct webp_state *ds = infile->dec_state;
	struct wuimg *img = infile->sub_img;
	if (ds->idx > state->frame) {
		rewind_webp_state(ds, img, state->frame);
	}
	while (ds->idx <= state->frame) {
		const enum wu_error err = webp_dec_frame(img, ds);
		if (err != wu_ok) {
			return err;
		}
	}
	return wu_ok;
}

static enum wu_error webp_callback(struct image_file *infile,
const struct wu_conf *wuconf, struct wu_state *state,
const enum image_event event) {
	(void)wuconf; (void)event;
	return webp_frame_iter(infile, state);
}

static enum wu_error gather_info(struct wuimg *img, WebPIterator *iter) {
	if (!wuimg_frames_init(img, (size_t)iter->num_frames)) {
		return wu_alloc_error;
	}

	size_t i = 0;
	do {
		const bool valid = wuimg_frame_set(img, i,
			(size_t)iter->x_offset, (size_t)iter->y_offset,
			(size_t)iter->width, (size_t)iter->height,
			iter->duration, 1000,
			iter->blend_method == WEBP_MUX_NO_BLEND);
		if (!valid) {
			return wu_alloc_error;
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

static enum wu_error homegrown_anim_setup(struct wuimg *img,
struct webp_state *ds, struct pix_rgba8 *bg_color) {
	struct homegrown_anim *hanim = &ds->anim.h;

	if (!WebPDemuxGetFrame(hanim->dmux, 1, &hanim->iter)) {
		return wu_decoding_error;
	}

	const enum wu_error st = wuimg_alloc(img);
	if (st != wu_ok) {
		return st;
	}
	*bg_color = get_bg_color(
		WebPDemuxGetI(hanim->dmux, WEBP_FF_BACKGROUND_COLOR));
	hanim->dispose.method = WEBP_MUX_DISPOSE_NONE;
	return gather_info(img, &hanim->iter);
}

static enum wu_error library_anim_setup(struct wuimg *img,
struct webp_state *ds, struct pix_rgba8 *bg_color) {
	const enum wu_error st = wuimg_verify(img);
	if (st != wu_ok) {
		return st;
	}
	WebPAnimDecoderOptions anim_opts;
	WebPAnimDecoderOptionsInit(&anim_opts);
	anim_opts.color_mode = MODE_BGRA;
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

static enum wu_error anim_setup(struct wuimg *img, struct webp_state *ds,
struct pix_rgba8 *bg) {
	if (ds->anim_render == webp_homegrown) {
		return homegrown_anim_setup(img, ds, bg);
	}
	return library_anim_setup(img, ds, bg);
}

static VP8StatusCode single_image_decode(struct wuimg *img,
struct webp_state *ds) {
	const bool alpha = ds->config.input.has_alpha;
	img->channels = alpha ? 4 : 3;
	WEBP_CSP_MODE colorspace;
	if (ds->config.input.format == 1) {
		struct image_planes *planes = wuimg_plane_init(img);
		if (!planes) {
			return VP8_STATUS_OUT_OF_MEMORY;
		}

		wuimg_plane_subsamp(img, 2, 2);
		img->cs.matrix = cicp_matrix_bt601_7;
		img->cs.limited = true;

		if (wuimg_alloc(img) != wu_ok) {
			return VP8_STATUS_OUT_OF_MEMORY;
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
		if (wuimg_alloc(img) != wu_ok) {
			return VP8_STATUS_OUT_OF_MEMORY;
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
	return WebPDecode(ds->data.bytes, ds->data.size, &ds->config);
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
	tree_add_leaf_utf8(tree, "Compression", fmt);
}

static enum wu_error webp_dec(struct image_file *infile,
const struct wu_conf *wuconf) {
	struct webp_state *ds = infile->dec_state;
	ds->data.size = infile->map.len;
	ds->data.bytes = infile->map.data;

	WebPInitDecoderConfig(&ds->config);
	VP8StatusCode status = WebPGetFeatures(ds->data.bytes, ds->data.size,
		&ds->config.input);
	if (status != VP8_STATUS_OK) {
		return wu_invalid_header;
	}

	read_metadata(&infile->metadata, ds,
		wuconf->webp_use_homegrown_renderer);

	struct wuimg *img = infile->sub_img;
	img->w = (size_t)ds->config.input.width;
	img->h = (size_t)ds->config.input.height;
	img->bitdepth = 8;
	if (wuimg_exceeds_limit(img, wuconf)) {
		return wu_exceeds_size_limit;
	}

	ds->config.options.bypass_filtering = wuconf->webp_bypass_filtering;
	ds->config.options.no_fancy_upsampling = wuconf->webp_fast_upsamp;
	ds->config.options.use_threads = true;
	ds->config.output.is_external_memory = true;

	enum wu_error err = wu_ok;
	if (ds->config.input.has_animation) {
		img->channels = 4;
		img->layout = pix_bgra;

		ds->anim_render = wuconf->webp_use_homegrown_renderer
			? webp_homegrown : webp_library;
		err = anim_setup(img, ds, &infile->bg);
		if (err == wu_ok) {
			err = webp_dec_frame(img, ds);
		}
	} else {
		status = single_image_decode(img, ds);
		if (status != VP8_STATUS_OK) {
			const char *msg;
			err = map_status(status, &msg);
			image_file_strerror_append(infile, msg);
		}
	}
	return err;
}

const struct image_fn webp_fn = {
	.mmap = true,
	.alloc_single = true,
	.state_size = sizeof(struct webp_state),
	.dec = webp_dec,
	.callback = webp_callback,
	.end = webp_end,
};

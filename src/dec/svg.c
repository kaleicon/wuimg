// SPDX-License-Identifier: 0BSD
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include <librsvg-2.0/librsvg/rsvg.h>

#include "wudefs.h"
#include "misc/endian.h"
#include "misc/file.h"
#include "misc/math.h"

static void svg_end(struct image_file *infile) {
	g_object_unref(infile->dec_state);
}

static enum wu_error handle_gerror(struct image_file *infile,
GError *err, const enum wu_error val) {
	image_file_strerror_append(infile, err->message);
	g_error_free(err);
	return val;
}

static float get_scale(const struct wu_state *state, const bool horizontal) {
	return (state->mirror && (state->rotate & 1) == horizontal)
		? -state->zoom : state->zoom;
}

static enum wu_error render_document(struct image_file *infile, cairo_t *canvas,
const RsvgRectangle *viewport) {
	GError *err = NULL;
	const bool success = rsvg_handle_render_document(infile->dec_state,
		canvas, viewport, &err);
	cairo_destroy(canvas);
	if (!success) {
		return handle_gerror(infile, err, wu_decoding_error);
	}
	return wu_ok;
}

static cairo_t * get_canvas(uint8_t *data, const cairo_format_t format,
const int width, const int height, const int stride) {
	cairo_surface_t *surf = cairo_image_surface_create_for_data(data,
		format, width, height, stride);
	if (cairo_surface_status(surf) != CAIRO_STATUS_SUCCESS) {
		cairo_surface_destroy(surf);
		return NULL;
	}

	cairo_t *canvas = cairo_create(surf);
	cairo_surface_destroy(surf);
	if (cairo_status(canvas) != CAIRO_STATUS_SUCCESS) {
		cairo_destroy(canvas);
		return NULL;
	}
	return canvas;
}

static enum wu_error adapt_to_window(struct image_file *infile,
const struct wu_conf *wuconf, struct wu_state *state,
const enum image_event event) {
	// wtf
	if ((unsigned)imax(state->fb.w, state->fb.h) > wuconf->max_img_size) {
		return wu_exceeds_size_limit;
	}

	struct wuimg *img = infile->sub_img;
	const size_t prev_size = wuimg_size(img);

	const cairo_format_t format = CAIRO_FORMAT_ARGB32;
	const int width = state->fb.w;
	const int height = state->fb.h;
	const int stride = cairo_format_stride_for_width(format, width);

	img->w = (size_t)width;
	img->h = (size_t)height;
	img->align_sh = strip_alignment((size_t)stride, img->w * img->channels,
		img->bitdepth);
	const enum wu_error st = wuimg_verify(img);
	if (st != wu_ok) {
		return st;
	}

	const size_t size = wuimg_size(img);
	if (size != prev_size) {
		free(img->data);
		img->data = calloc(size, 1);
		if (!img->data) {
			return wu_alloc_error;
		}
	} else {
		memset(img->data, 0, size);
	}

	cairo_t *canvas = get_canvas(img->data, format, width, height,
		stride);
	if (!canvas) {
		return wu_alloc_error;
	}

	const double fbw = (double)state->fb.w / 2.0;
	const double fbh = (double)state->fb.h / 2.0;
	double x_scale = 1;
	double y_scale = 1;
	double x = fbw;
	double y = fbh;
	double rotate = 0;
	const bool first_render = event & ev_subcycle;
	if (!first_render) {
		x_scale = get_scale(state, true);
		y_scale = get_scale(state, false);
		rotate = state->rotate * M_PI_2;
		x = fma(state->x_offset, x_scale, fbw);
		y = fma(state->y_offset, y_scale, fbh);
	}
	const RsvgRectangle viewport = {
		.x = -fbw,
		.y = -fbh,
		.width = (double)state->fb.w,
		.height = (double)state->fb.h,
	};

	cairo_translate(canvas, x, y);
	cairo_scale(canvas, x_scale, y_scale);
	cairo_rotate(canvas, rotate);
	return render_document(infile, canvas, &viewport);
}

static enum wu_error attempt_native(struct image_file *infile,
const struct wu_conf *wuconf) {
	RsvgRectangle viewport;
	if (rsvg_handle_get_intrinsic_size_in_pixels(infile->dec_state,
	&viewport.width, &viewport.height)) {
		viewport.x = 0;
		viewport.y = 0;
	} else if (!rsvg_handle_get_geometry_for_element(infile->dec_state,
	NULL, NULL, &viewport, NULL)) {
		viewport = (RsvgRectangle) {
			.x = 0, .y = 0,
			.width = 1280, .height = 1280,
		};
	}

	const double mis = (double)wuconf->max_img_size;
	double scale = fmin(1, mis / fmax(viewport.width, viewport.height));
	viewport.x *= scale;
	viewport.y *= scale;
	viewport.width *= scale;
	viewport.height *= scale;

	const cairo_format_t format = CAIRO_FORMAT_ARGB32;
	const int width = (int)ceil(viewport.width);
	const int height = (int)ceil(viewport.width);
	const int stride = cairo_format_stride_for_width(format, width);

	struct wuimg *img = infile->sub_img;
	img->w = (size_t)width;
	img->h = (size_t)height;
	img->align_sh = strip_alignment((size_t)stride, img->w * img->channels,
		img->bitdepth);
	const enum wu_error st = wuimg_alloc(img);
	if (st != wu_ok) {
		return st;
	}

	cairo_t *canvas = get_canvas(img->data, format, width, height, stride);
	if (!canvas) {
		return wu_alloc_error;
	}
	return render_document(infile, canvas, &viewport);
}

static enum wu_error svg_callback(struct image_file *infile,
const struct wu_conf *wuconf, struct wu_state *state,
const enum image_event event) {
	if (event & (ev_subcycle | ev_scale | ev_move | ev_mirrot)) {
		if (wuconf->svg_window_adapt) {
			return adapt_to_window(infile, wuconf, state, event);
		} else if (!infile->sub_img->data) {
			return attempt_native(infile, wuconf);
		}
	}
	return wu_no_change;
}

static enum wu_error svg_dec(struct image_file *infile,
const struct wu_conf *wuconf) {
	(void)wuconf;
	GError *err = NULL;
	infile->dec_state = rsvg_handle_new_from_data(infile->map.ptr,
		infile->map.len, &err);
	if (!infile->dec_state) {
		return handle_gerror(infile, err, wu_open_error);
	}

	struct wuimg *img = infile->sub_img;
	img->channels = 4;
	img->bitdepth = 8;
	img->alpha = alpha_associated;
	img->scalable = wuconf->svg_window_adapt;
	/* Cairo renders in ARGB, which on little-endian means BGRA. */
	switch (which_end()) {
	case little_endian: img->layout = pix_bgra; break;
	case big_endian: img->layout = pix_argb; break;
	}
	return wu_ok;
}

const struct image_fn svg_fn = {
	.mmap = true,
	.alloc_single = true,
	.dec = svg_dec,
	.callback = svg_callback,
	.end = svg_end,
};

// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2020 kaleido
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include <librsvg-2.0/librsvg/rsvg.h>

#include "wudefs.h"
#include "misc/endian.h"
#include "misc/math.h"

static void end_svg(struct image_file *infile) {
	g_object_unref(infile->dec_state);
}

static struct wu_st handle_gerror(struct image_file *infile, GError *err,
const struct wu_st val) {
	image_file_strerror_append(infile, err->message);
	g_error_free(err);
	return val;
}

static struct wu_st render_svg(struct image_file *infile, cairo_t *canvas,
const RsvgRectangle *viewport) {
	GError *err = NULL;
	const bool success = rsvg_handle_render_document(infile->dec_state,
		canvas, viewport, &err);
	cairo_destroy(canvas);
	if (!success) {
		return handle_gerror(infile, err, WUERR_HERE(wu_decoding_error));
	}
	return WU_OK;
}

static cairo_t * get_cairo_canvas(uint8_t *data, const cairo_format_t format,
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

static struct wu_st adapt_to_window(struct image_file *infile,
struct wu_state *state, const enum image_event event) {
	// am i too paranoid?
	if ((unsigned)imax(state->fb.w, state->fb.h) > infile->conf->max_img_size) {
		return WUERR_HERE(wu_exceeds_size_limit);
	}

	const cairo_format_t format = CAIRO_FORMAT_ARGB32;
	const int width = state->fb.w;
	const int height = state->fb.h;
	const int stride = cairo_format_stride_for_width(format, width);

	struct wuimg *img = infile->sub_img;
	const size_t prev_size = wuimg_size(img);
	img->w = (size_t)width;
	img->h = (size_t)height;
	img->align_sh = strip_alignment((size_t)stride, img->w * img->channels,
		img->bitdepth);
	const enum wu_error st = wuimg_verify(img);
	if (st != wu_ok) {
		return WUERR_HERE(st);
	}

	const size_t size = wuimg_size(img);
	if (size != prev_size) {
		free(img->data);
		img->data = calloc(size, 1);
		if (!img->data) {
			return WUERR_HERE(wu_alloc_error);
		}
	} else {
		memset(img->data, 0, size);
	}

	cairo_t *canvas = get_cairo_canvas(img->data, format, width, height,
		stride);
	if (!canvas) {
		return WUERR_HERE(wu_alloc_error);
	}

	const double fbw = (double)state->fb.w * .5;
	const double fbh = (double)state->fb.h * .5;
	double x_scale = 1;
	double y_scale = 1;
	double x = fbw;
	double y = fbh;
	double rotate = 0;
	const bool first_render = event & ev_subcycle;
	if (!first_render) {
		rotate = state->rotate * M_PI_2;
		x = fma(state->x_offset, state->zoom, fbw);
		y = fma(state->y_offset, state->zoom, fbh);
		x_scale = state->zoom;
		y_scale = state->mirror ? -state->zoom : state->zoom;
	}
	const RsvgRectangle viewport = {
		.x = -fbw,
		.y = -fbh,
		.width = (double)state->fb.w,
		.height = (double)state->fb.h,
	};

	cairo_translate(canvas, x, y);
	cairo_rotate(canvas, rotate);
	cairo_scale(canvas, x_scale, y_scale);
	return render_svg(infile, canvas, &viewport);
}

static struct wu_st attempt_native(struct image_file *infile) {
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

	const double mis = (double)infile->conf->max_img_size;
	double scale = fmin(1, mis / fmax(viewport.width, viewport.height));
	viewport.x *= scale;
	viewport.y *= scale;
	viewport.width *= scale;
	viewport.height *= scale;

	const cairo_format_t format = CAIRO_FORMAT_ARGB32;
	const int width = (int)ceil(viewport.width);
	const int height = (int)ceil(viewport.height);
	const int stride = cairo_format_stride_for_width(format, width);

	struct wuimg *img = infile->sub_img;
	img->w = (size_t)width;
	img->h = (size_t)height;
	img->align_sh = strip_alignment((size_t)stride, img->w * img->channels,
		img->bitdepth);
	const enum wu_error st = wuimg_alloc(img);
	if (st != wu_ok) {
		return WUERR_HERE(st);
	}

	cairo_t *canvas = get_cairo_canvas(img->data, format, width, height, stride);
	if (!canvas) {
		return WUERR_HERE(wu_alloc_error);
	}
	return render_svg(infile, canvas, &viewport);
}

static struct wu_st event_svg(struct image_file *infile,
struct wu_state *state, const enum image_event event) {
	if (event & (ev_subcycle | ev_transform)) {
		if (infile->conf->svg_window_adapt) {
			return adapt_to_window(infile, state, event);
		} else if (!infile->sub_img->data) {
			return attempt_native(infile);
		}
	}
	return WU_NO_CHANGE;
}

static struct wu_st init_svg(struct image_file *infile) {
	GError *err = NULL;
	infile->dec_state = rsvg_handle_new_from_data(infile->map.ptr,
		infile->map.len, &err);
	if (!infile->dec_state) {
		return handle_gerror(infile, err, WUERR_HERE(wu_open_error));
	}

	struct wuimg *img = infile->sub_img;
	img->channels = 4;
	img->bitdepth = 8;
	img->alpha = alpha_associated;
	img->scalable = infile->conf->svg_window_adapt;
	/* Cairo renders in ARGB, which on little-endian means BGRA. */
	switch (which_end()) {
	case little_endian: img->layout = pix_bgra; break;
	case big_endian: img->layout = pix_argb; break;
	}
	return WU_OK;
}

const struct image_fn svg_fn = {
	.mmap = true,
	.alloc_single = true,
	.init = init_svg,
	.event = event_svg,
	.end = end_svg,
};

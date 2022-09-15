#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include <librsvg-2.0/librsvg/rsvg.h>

#include "wudefs.h"
#include "common/endian.h"
#include "common/file.h"
#include "common/math.h"

struct svg_state {
	RsvgHandle *handle;
	RsvgRectangle viewport;
	float dec_scale;
};

static void clean_svg_state(struct image_file *infile) {
	struct svg_state *ds = infile->dec_state;
	g_object_unref(ds->handle);
}

static float limit_zoom(const float zoom, const RsvgRectangle *viewport,
unsigned int limit, bool *reached_limit) {
	const unsigned int cairo_size_limit = 1 << 14;
	const double flimit = (double)umin(limit, cairo_size_limit);

	const double max = fmax(viewport->width, viewport->height) * zoom;
	if (max > flimit) {
		*reached_limit = true;
		return (float)(zoom * (flimit / max));
	}
	return zoom;
}

static enum wu_error svg_render(struct raw_img *img, struct svg_state *ds) {
	const cairo_format_t format = CAIRO_FORMAT_ARGB32;
	const int width = (int)ceil((ds->viewport.width * ds->dec_scale));
	const int height = (int)ceil((ds->viewport.height * ds->dec_scale));
	const int stride = cairo_format_stride_for_width(format, width);

	free(img->data);
	img->data = calloc((size_t)(stride * height), 1);
	if (!img->data) {
		return wu_alloc_error;
	}
	cairo_surface_t *surf = cairo_image_surface_create_for_data(img->data,
		format, width, height, stride);
	if (cairo_surface_status(surf) != CAIRO_STATUS_SUCCESS) {
		cairo_surface_destroy(surf);
		return wu_alloc_error;
	}

	img->w = (size_t)width;
	img->h = (size_t)height;
	const enum wu_error st = raw_img_verify(img);
	if (st != wu_ok) {
		cairo_surface_destroy(surf);
		return st;
	}

	cairo_t *canvas = cairo_create(surf);
	cairo_surface_destroy(surf);
	if (cairo_status(canvas) != CAIRO_STATUS_SUCCESS) {
		cairo_destroy(canvas);
		return wu_alloc_error;
	}

	cairo_scale(canvas, ds->dec_scale, ds->dec_scale);
	const bool success = rsvg_handle_render_document(ds->handle, canvas,
		&ds->viewport, NULL);
	cairo_destroy(canvas);
	if (success) {
		fprintf(stderr, "Rendered @ %zu x %zu (%zu bytes), %.2fx original\n",
			img->w, img->h, img->w * img->h * img->channels,
			ds->dec_scale);
		return wu_ok;
	}
	return wu_decoding_error;
}

static enum wu_error svg_rescale(struct image_file *infile,
const struct wu_conf *wuconf, struct wu_state *state) {
	const float eps = 1.0f / (float)imin(wuconf->fb.w, wuconf->fb.h);
	if (state->zoom <= 1 + eps) {
		if (wuconf->svg_redraw == svg_upscale || state->zoom >= 1 - eps) {
			return wu_no_change;
		}
	}

	struct svg_state *ds = infile->dec_state;

	bool reached_limit = false;
	const float new_zoom = limit_zoom(ds->dec_scale * state->zoom,
		&ds->viewport, wuconf->max_img_size, &reached_limit);
	if (wuconf->svg_redraw == svg_upscale && reached_limit) {
		infile->events = 0;
	}

	if (new_zoom > ds->dec_scale
	|| (wuconf->svg_redraw == svg_anyscale && new_zoom != ds->dec_scale)) {
		state->zoom = 1;
		state->x_offset *= new_zoom / ds->dec_scale;
		state->y_offset *= new_zoom / ds->dec_scale;
		ds->dec_scale = new_zoom;
		return svg_render(infile->sub_img, ds);
	}
	return wu_no_change;
}

enum wu_error svg_callback(struct image_file *infile,
const struct wu_conf *wuconf, struct wu_state *state,
const enum image_event event) {
	if (event & ev_scale) {
		return svg_rescale(infile, wuconf, state);
	}
	clean_svg_state(infile);
	return wu_ok;
}

enum wu_error svg_dec(struct image_file *infile,
const struct wu_conf *wuconf) {
	struct svg_state *ds = calloc(sizeof(*ds), 1);
	if (!ds) {
		return wu_alloc_error;
	}
	infile->dec_state = ds;

	struct map_info map;
	if (!file_map(&map, infile->ifp)) {
		return wu_alloc_error;
	}

	ds->handle = rsvg_handle_new_from_data(map.data, map.len, NULL);
	file_unmap(&map);
	if (!ds->handle) {
		return wu_open_error;
	}

	ds->viewport.x = 0;
	ds->viewport.y = 0;
	if (!rsvg_handle_get_intrinsic_size_in_pixels(ds->handle,
	&ds->viewport.width, &ds->viewport.height)) {
		ds->viewport.width = (double)wuconf->fb.w;
		ds->viewport.height = (double)wuconf->fb.h;
	}

	struct raw_img *img = alloc_sub_images(infile, 1);
	if (!img) {
		return wu_alloc_error;
	}
	img->channels = 4;
	img->bitdepth = 8;
	/* Cairo renders in ARGB, which on little-endian means BGRA. */
	switch (which_end()) {
	case little_endian: img->layout = pix_bgra; break;
	case big_endian: img->layout = pix_argb; break;
	}
	img->alpha = alpha_associated;

	bool reached_limit = false;
	ds->dec_scale = limit_zoom(1, &ds->viewport, wuconf->max_img_size,
		&reached_limit);

	const enum wu_error err = svg_render(img, ds);
	if (err == wu_ok && !reached_limit) {
		switch (wuconf->svg_redraw) {
		case svg_upscale: infile->events = ev_upscale; break;
		case svg_anyscale: infile->events = ev_scale; break;
		default: break;
		}
	}
	return err;
}

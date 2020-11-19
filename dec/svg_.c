#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include <librsvg-2.0/librsvg/rsvg.h>

#include "../wudefs.h"
#include "../common.h"

struct svg_state {
	RsvgDimensionData dims;
	cairo_surface_t *record;
	float dec_scale;
};

static void clean_svg_state(struct image_file *infile) {
	struct svg_state *ds = infile->dec_state;
	cairo_surface_destroy(ds->record);
	free(ds);
	infile->dec_state = NULL;
	infile->events = 0;
}

static float limit_zoom(float zoom, RsvgDimensionData *dims,
unsigned int limit, bool *reached_limit) {
	const unsigned int rsvg_size_limit = 1 << 14;
	limit = umin(limit, rsvg_size_limit);

	const float max = (float)imax(dims->width, dims->height) * zoom;
	if (max > (float)limit) {
		zoom *= (float)limit / max;
		if (reached_limit) {
			*reached_limit = true;
		}
	}
	return zoom;
}

static enum wu_error svg_render(struct image_file *infile,
struct svg_state *ds) {
	struct raw_img *img = infile->sub_img;

	const cairo_format_t format = CAIRO_FORMAT_ARGB32;
	const int width = (int)((float)ds->dims.width * ds->dec_scale);
	const int height = (int)((float)ds->dims.height * ds->dec_scale);
	const int stride = cairo_format_stride_for_width(format, width);

	free(img->data);
	img->data = calloc((size_t)(stride * height), 1);
	if (!img->data) {
		clean_svg_state(infile);
		return wu_alloc_error;
	}
	cairo_surface_t *surf = cairo_image_surface_create_for_data(img->data,
		format, width, height, stride);
	if (cairo_surface_status(surf) != CAIRO_STATUS_SUCCESS) {
		cairo_surface_destroy(surf);
		clean_svg_state(infile);
		return wu_alloc_error;
	}

	img->w = (size_t)cairo_image_surface_get_width(surf);
	img->h = (size_t)cairo_image_surface_get_height(surf);

	cairo_t *canvas = cairo_create(surf);
	cairo_surface_destroy(surf);
	if (cairo_status(canvas) != CAIRO_STATUS_SUCCESS) {
		cairo_destroy(canvas);
		clean_svg_state(infile);
		return wu_alloc_error;
	}

	cairo_scale(canvas, ds->dec_scale, ds->dec_scale);
	cairo_set_source_surface(canvas, ds->record, 0, 0);
	cairo_paint(canvas);
	cairo_destroy(canvas);

	printf("Rendered @ %zu x %zu (%zu bytes), %.2fx original\n", img->w,
		img->h, img->w * img->h * img->channels, ds->dec_scale);
	return wu_ok;
}

static enum wu_error svg_rescale(struct image_file *infile,
const struct wu_conf *wuconf, struct wu_state *state) {
	if (state->zoom == 1.0f) {
		return wu_no_change;
	} else if (wuconf->svg_redraw == upscale && state->zoom < 1.0f) {
		return wu_no_change;
	}

	struct svg_state *ds = infile->dec_state;

	bool reached_limit = false;
	const float new_zoom = limit_zoom(ds->dec_scale * state->zoom,
		&ds->dims, wuconf->max_img_size, &reached_limit);
	if (wuconf->svg_redraw == upscale && reached_limit) {
		infile->events = 0;
	}

	if (new_zoom > ds->dec_scale
	|| (wuconf->svg_redraw == anyscale && new_zoom != ds->dec_scale)) {
		state->x_offset *= new_zoom / ds->dec_scale;
		state->y_offset *= new_zoom / ds->dec_scale;
		ds->dec_scale = new_zoom;
		state->zoom = 1;
		return svg_render(infile, ds);
	}
	return wu_no_change;
}

enum wu_error svg_callback(struct image_file *infile,
const struct wu_conf *wuconf, struct wu_state *state,
const enum image_event event) {
	enum wu_error status = wu_ok;
	if (event & scale) {
		status = svg_rescale(infile, wuconf, state);
	}
	if (event == 0 || infile->events == 0) {
		clean_svg_state(infile);
	}
	return status;
}

enum wu_error svg_dec(struct image_file *infile,
const struct wu_conf *wuconf) {
	const struct mmap_file map = mmap_stream(infile->ifp);
	if (map.data == MAP_FAILED) {
		return wu_alloc_error;
	}

	RsvgHandle *handle = rsvg_handle_new_from_data(map.data, map.len, NULL);
	if (!handle) {
		munmap_stream(map);
		return wu_open_error;
	}

	struct svg_state *ds = malloc(sizeof(*ds));
	if (!ds) {
		g_object_unref(handle);
		munmap_stream(map);
		return wu_alloc_error;
	}
	infile->dec_state = ds;
	rsvg_handle_get_dimensions(handle, &ds->dims);
	ds->record = cairo_recording_surface_create(CAIRO_CONTENT_COLOR_ALPHA,
		NULL);

	cairo_t *canvas = cairo_create(ds->record);
	if (cairo_status(canvas) != CAIRO_STATUS_SUCCESS) {
		cairo_destroy(canvas);
		g_object_unref(handle);
		munmap_stream(map);
		clean_svg_state(infile);
		return wu_alloc_error;
	}

	const bool success = rsvg_handle_render_cairo(handle, canvas);
	cairo_destroy(canvas);
	g_object_unref(handle);
	munmap_stream(map);
	if (!success) {
		clean_svg_state(infile);
		return wu_decoding_error;
	}
	struct raw_img *img = alloc_sub_images(infile, 1);
	if (!img) {
		clean_svg_state(infile);
		return wu_alloc_error;
	}
	img->channels = 4;
	img->bitdepth = 8;
	img->layout = bgra; /* Cairo renders in ARGB, which on little-endian
		means BGRA. */

	bool reached_limit = false;
	ds->dec_scale = limit_zoom(1, &ds->dims, wuconf->max_img_size,
		&reached_limit);

	enum wu_error err = svg_render(infile, ds);
	if (err == wu_ok) {
		if (wuconf->svg_redraw == never || reached_limit) {
			clean_svg_state(infile);
		} else {
			switch (wuconf->svg_redraw) {
			case upscale: infile->events = up_scale; break;
			case anyscale: infile->events = scale; break;
			default: break;
			}
		}
	} else {
		clean_svg_state(infile);
	}
	return err;
}

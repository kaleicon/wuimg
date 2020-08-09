#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include <librsvg-2.0/librsvg/rsvg.h>

#include "wudefs.h"
#include "common.h"

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

static enum wu_error svg_render(struct image_file *infile,
const struct wu_conf *wuconf, struct svg_state *ds) {
	struct raw_img *img = infile->sub_img;

	const cairo_format_t format = CAIRO_FORMAT_ARGB32;
	const int width = (int)((float)ds->dims.width * ds->dec_scale);
	const int height = (int)((float)ds->dims.height * ds->dec_scale);
	const int stride = cairo_format_stride_for_width(format, width);
	if ((unsigned int)imax(width, height) > wuconf->max_img_size) {
		return wu_ok;
	}

	unsigned char *new_data = calloc((size_t)(stride * height), 1);
	if (!new_data) {
		clean_svg_state(infile);
		return wu_alloc_error;
	}
	cairo_surface_t *surf = cairo_image_surface_create_for_data(new_data,
		format, width, height, stride);
	if (cairo_surface_status(surf) != CAIRO_STATUS_SUCCESS) {
		cairo_surface_destroy(surf);
		clean_svg_state(infile);
		return wu_alloc_error;
	}

	img->w = (size_t)cairo_image_surface_get_width(surf);
	img->h = (size_t)cairo_image_surface_get_height(surf);

	cairo_t *canvas = cairo_create(surf);
	if (cairo_status(canvas) != CAIRO_STATUS_SUCCESS) {
		cairo_destroy(canvas);
		cairo_surface_destroy(surf);
		clean_svg_state(infile);
		return wu_alloc_error;
	}

	if (wuconf->svg_antialiasing) {
		cairo_antialias_t alias;
		switch (wuconf->svg_antialiasing) {
		case fast:
			alias = CAIRO_ANTIALIAS_FAST;
			break;
		case good:
			alias = CAIRO_ANTIALIAS_GOOD;
			break;
		case best:
			alias = CAIRO_ANTIALIAS_BEST;
			break;
		default:
			alias = CAIRO_ANTIALIAS_DEFAULT;
			break;
		}
		cairo_set_antialias(canvas, alias);
	}
	cairo_scale(canvas, ds->dec_scale, ds->dec_scale);
	cairo_set_source_surface(canvas, ds->record, 0, 0);
	cairo_paint(canvas);
	cairo_destroy(canvas);
	cairo_surface_destroy(surf);

	free(img->data);
	img->data = new_data;
	printf("Re-render @ %zu x %zu (%zu bytes), %.2fx original\n", img->w,
		img->h, img->w * img->h * img->channels, ds->dec_scale);
	return wu_ok;
}

static float limit_zoom(float zoom, const int width, const int height,
const unsigned int limit, bool *reached_limit) {
	const float max = (float)imax(width, height) * zoom;
	if (max > (float)limit) {
		zoom *= (float)limit / max;
		if (reached_limit) {
			*reached_limit = true;
		}
	}
	return zoom;
}

static enum wu_error svg_rescale(struct image_file *infile,
const struct wu_conf *wuconf, struct wu_state *state) {
	if (state->zoom == 1.0f) {
		return wu_ok;
	} else if (wuconf->svg_redraw == upscale && state->zoom < 1.0f) {
		return wu_ok;
	}

	struct svg_state *ds = infile->dec_state;

	bool reached_limit = false;
	const float new_zoom = limit_zoom(ds->dec_scale * state->zoom,
		ds->dims.width, ds->dims.height, wuconf->max_img_size,
		&reached_limit);
	if (wuconf->svg_redraw == upscale && reached_limit) {
		infile->events = 0;
	}

	if (new_zoom > ds->dec_scale
	|| (wuconf->svg_redraw == always && new_zoom != ds->dec_scale)) {
		state->x_offset *= new_zoom / ds->dec_scale;
		state->y_offset *= new_zoom / ds->dec_scale;
		ds->dec_scale = new_zoom;
		state->zoom = 1;
		return svg_render(infile, wuconf, ds);
	}
	return wu_ok;
}

enum wu_error svg_callback(struct image_file *infile,
const struct wu_conf *wuconf, struct wu_state *state,
const enum image_event event) {
	enum wu_error status = wu_ok;
	if (event == scale) {
		status = svg_rescale(infile, wuconf, state);
	}
	if (event == 0 || infile->events == 0) {
		clean_svg_state(infile);
	}
	return status;
}

enum wu_error svg_dec(struct image_file *infile,
const struct wu_conf *wuconf) {
	size_t size;
	unsigned char *data = read_file_to_mem(infile->ifp, &size);
	if (!data) {
		return wu_alloc_error;
	}

	RsvgHandle *handle = rsvg_handle_new_from_data(data, size, NULL);
	if (!handle) {
		free(data);
		return wu_open_error;
	}

	struct svg_state *ds = malloc(sizeof(*ds));
	if (!ds) {
		g_object_unref(handle);
		free(data);
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
		free(data);
		clean_svg_state(infile);
		return wu_alloc_error;
	}

	const bool success = rsvg_handle_render_cairo(handle, canvas);
	cairo_destroy(canvas);
	g_object_unref(handle);
	free(data);
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
	ds->dec_scale = limit_zoom(1, ds->dims.width, ds->dims.height,
		wuconf->max_img_size, NULL);

	enum wu_error err = svg_render(infile, wuconf, ds);
	if (err == wu_ok && wuconf->svg_redraw != never) {
		infile->events = scale;
	} else {
		clean_svg_state(infile);
	}
	return err;
}

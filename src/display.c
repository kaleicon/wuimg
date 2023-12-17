// SPDX-License-Identifier: 0BSD
#include <ctype.h>
#include <math.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "dec.h"
#include "display.h"
#include "events.h"
#include "misc/math.h"
#include "misc/mem.h"

static void set_background_color(const struct image_context *image) {
	const struct wu_conf *conf = &image->conf;
	if (conf->bg_src != bg_default) {
		const struct image_file *infile = &image->file;

		uint8_t bg[4];
		if (memchk(&infile->bg, 0, sizeof(infile->bg))) {
			memcpy(bg, &infile->bg, sizeof(bg));
			bg[3] = conf->bg[3];
		} else {
			memcpy(bg, conf->bg, sizeof(bg));
		}
		gl_clear_color(bg);
	}
}

void display_end(struct window_context *window, const struct term_restore *tr) {
	gl_terminate(&window->pub.gl);
	window_terminate(window);
	if (tr) {
		term_noncanon_end(tr);
	}
}

static bool update_texture(struct image_context *image,
struct window_context *window, const bool reset) {
	struct gl_context *gl = &window->pub.gl;
	struct wu_state *state = &image->state;
	struct wuimg *img = image->file.sub_img + state->idx;
	switch (gl_texture_upload(gl, img)) {
	case gl_upload_fail:
		term_line_put("Failed to upload to texture.", stderr);
		return false;
	case gl_upload_success:
		if (reset) {
			const float min = (float)(image->conf.magnify_under /
				(zumin(img->w, img->h) + 1) + 1);
			state->zoom = fminf(min, gl->tex.fit_zoom);
			state->rotate = 0;
			state->mirror = 0;
			state->x_offset = 0;
			state->y_offset = 0;
		}
		break;
	case gl_upload_same_size:
		break;
	}

	if (!state->anim_playing) {
		fprintf(stderr, "Frame %d uploaded in %" PRIu64 " ns\n",
			state->frame, gl_clock_query(gl));
	}
	return true;
}

static double draw_rest_poll(struct window_context *window,
const bool print_draw_time) {
	uint64_t draw_time = 0;
	if (window_draw(window)) {
		draw_time = gl_clock_query(&window->pub.gl);
		if (print_draw_time) {
			nanosec_report("Drawn", draw_time, report_info);
		}
	}

	uint32_t refresh = window->pub.win.refresh_nsec;
	if (!refresh) {
		refresh = 1000000000 / 60;
	}
	const struct timespec tm = {
		.tv_nsec = (long)refresh - (long)draw_time*2,
	};
	nanosleep(&tm, NULL);

	window_poll(window);
	unsigned char tk[8];
	const size_t read = term_event_read(tk, sizeof(tk));
	for (size_t i = 0; i < read; ++i) {
		window_key_add(&window->pub.held_keys, key_external,
			tk[i], isupper(tk[i]));
	}
	return event_exec(window);
}

static double idle_display(struct image_context *image,
struct window_context *window, double remaining) {
	struct wu_event *event = &window->pub.event;
	struct wu_state *state = &window->pub.image.state;

	for (bool first = true;; first = false) {
		event->image = 0;
		const double ellapsed = draw_rest_poll(window,
			!state->anim_playing && first);
		if (state->anim_playing && window->pub.win.focused) {
			remaining -= ellapsed;
			if (remaining <= 0) {
				event->image = image_frame_cycle(image, 1);
			}
		}

		if (event->cycle || event->program || event->rm == rm_yes) {
			break;
		} else if (event->image) {
			window->pub.gl.update = gl_update_matrix;
			if (event->image & image->file.events
			|| event->image & ev_subcycle) {
				break;
			}
		}
	}
	return remaining;
}

static double min_time(const struct wuimg *img, const struct wu_state *state) {
	struct image_frames *frames = img->frames;
	if (frames) {
		return fmax(frames->f[state->frame].sec, 1.0 / 30);
	}
	return 0;
}

bool display_loop(struct window_context *window, const bool single_file,
const bool allow_delete) {
	struct image_context *image = &window->pub.image;
	struct image_file *infile = &image->file;
	struct wu_state *state = &window->pub.image.state;
	struct wu_event *event = &window->pub.event;

	*event = (struct wu_event){
		.rm = allow_delete ? rm_no : rm_disable,
		.image = ev_subcycle, // for init only, not passed to image
	};

	set_background_color(image);
	window_set_title(window, image->name);

	bool all_ok = true;
	double remaining = 0;
	for (bool upload = true, first_iter = true;;) {
		if (upload) {
			const struct wuimg *img = infile->sub_img + state->idx;
			if (event->image & ev_subcycle) {
				state->anim_playing = wuimg_frames_nr(img) > 1;
			}

			all_ok = update_texture(image, window, first_iter);
			if (!all_ok) {
				break;
			}
			image_file_free_if_single(infile);

			if (state->anim_playing) {
				const double display_time = min_time(img, state);
				remaining = fmax(0, remaining + display_time);
			}
			upload = false;
			first_iter = false;
			event->image = 0;
		}

		remaining = idle_display(image, window, remaining);

		if ((!single_file && event->cycle)
		|| event->program || event->rm == rm_yes) {
			break;
		} else if (event->image) {
			if (event->image & infile->events) {
				const enum wu_error err = dec_callback(
					image, event->image);
				if (err == wu_ok) {
					upload = true;
				} else if (err != wu_no_change) {
					term_line_key_val("Callback failed",
						wu_error_message(err), stdout);
					all_ok = false;
					break;
				}
			}
			if (event->image & (ev_subcycle | ev_frame)) {
				upload = true;
			}
		}
	}
	term_line_clear();
	return all_ok;
}

bool display_setup(struct window_context *window, struct term_restore *tr) {
	const watch_t start = watch_look();
	if (!window_setup(window)) {
		term_line_put("Failed to create window", stderr);
		return false;
	}

	term_line_key_val("Window backend", window->backend, stderr);

	if (!gl_context_setup(&window->pub.gl, &window->pub.image.conf)) {
		window_terminate(window);
		term_line_put("Failed to configure OpenGL context", stderr);
		return false;
	}

	window_postgl_setup(window);

	if (tr) {
		term_noncanon_start(tr);
	}
	watch_report("Display set", start, report_info);
	return true;
}

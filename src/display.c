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

static enum wu_error decode_with_stats(struct image_context *image) {
	const watch_t start = watch_look();
	const enum wu_error result = dec_decode(image);
	const watch_t diff = watch_elapsed(start);

	const struct image_file *infile = &image->file;
	const char *what = "Failed";
	if (result == wu_ok) {
		image_file_print(infile, 1, infile->nr == 1);
		what = "Decoded";
	} else {
		term_line_key_val("Decoding error",
			wu_error_message(result), stdout);
		if (infile->errors.str) {
			fputs("Library message: ", stdout);
			wustr_print(&infile->errors, stdout);
		}
	}
	nanosec_report(what, diff, report_normal);
	return result;
}

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

static enum wu_error update_texture(struct image_context *image,
struct gl_context *gl, const bool reset) {
	struct wu_state *state = &image->state;
	struct wuimg *img = image->file.sub_img + state->idx;
	switch (gl_texture_upload(gl, img, &image->conf)) {
	case gl_upload_fail:
		term_line_put("Failed to upload to texture.", stderr);
		return wu_display_error;
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
	return wu_ok;
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

	window_poll(window, 0);
	const unsigned char ev = term_queue_next(&window->pub.term);
	window_key_add(&window->pub.held_keys, key_external, ev, isupper(ev));
	return event_exec(window);
}

static bool idle_display(struct image_context *image,
struct window_context *window, const double next_frame,
const enum image_event evs, const bool allow_cycle) {
	struct wu_event *event = &window->pub.event;
	struct wu_state *state = &window->pub.image.state;

	for (bool print_time = true;; print_time = !state->anim_playing) {
		event->image = ev_time;
		const double elapsed = draw_rest_poll(window, print_time);
		if (state->anim_playing && window->pub.win.focused) {
			state->time += (float)elapsed;
			if (state->time >= next_frame && image_frame_cycle(image, 1)) {
				event->image = ev_frame;
			}
		}

		if ((allow_cycle && event->cycle)
		|| event->exit || event->rm == rm_yes) {
			return true;
		} else if (event->image) {
			if (event->image & ~ev_time) {
				window->pub.gl.update = gl_update_matrix;
			}
			if (event->image & evs) {
				break;
			}
		}
	}
	return false;
}

static double min_time(const struct wuimg *img, const struct wu_state *state) {
	struct image_frames *frames = img->frames;
	if (frames) {
		return fmax(frames->f[state->frame].sec, 1.0 / 30);
	}
	return INFINITY;
}

enum wu_error display_loop(struct window_context *window,
const bool allow_cycle, const bool allow_delete) {
	struct image_context *image = &window->pub.image;
	enum wu_error err = decode_with_stats(image);
	if (err != wu_ok) {
		return err;
	}

	struct image_file *infile = &image->file;
	struct wu_state *state = &window->pub.image.state;
	struct wu_event *event = &window->pub.event;

	*event = (struct wu_event){
		.rm = allow_delete ? rm_no : rm_never,
		.image = ev_subcycle, // for init only, not passed to image
	};

	set_background_color(image);
	window_set_title(window, image->name);

	double next_frame = INFINITY;
	enum image_event evs = ev_subcycle;
	for (bool upload = true, first_iter = true; err == wu_ok;) {
		if (upload) {
			if (event->image & ev_subcycle) {
				state->anim_playing = image_cur_is_anim(image);
				state->time = 0;
				evs = image_cur_events(image);
			}

			struct gl_context *gl = &window->pub.gl;
			err = update_texture(image, gl, first_iter);
			if (err != wu_ok) {
				break;
			}

			image_file_free_if_single(infile);

			if (first_iter) {
				window_timer_update(&window->pub);
				first_iter = false;
			}

			const struct wuimg *img = infile->sub_img + state->idx;
			if (evs & ev_frame) {
				next_frame = state->time + min_time(img, state);
			} else {
				fprintf(stderr,
					"Frame %d uploaded in %" PRIu64 " ns\n",
					state->frame, gl_clock_query(gl));
			}
			upload = false;
			event->image = 0;
		}

		if (idle_display(image, window, next_frame, evs, allow_cycle)) {
			break;
		} else if (event->image & evs) {
			err = dec_callback(image, event->image);
			switch (err) {
			case wu_no_change:
				upload = event->image & (ev_subcycle | ev_frame);
				err = wu_ok;
				break;
			case wu_ok:
				upload = true;
				break;
			default:
				term_line_key_val("Callback failed",
					wu_error_message(err), stdout);
				fputs("Library message: ", stdout);
				wustr_print(&infile->errors, stdout);
				break;
			}
		}
	}
	term_line_clear();
	return err;
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

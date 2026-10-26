// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2020 kaleido
#include <ctype.h>
#include <limits.h>
#include <string.h>

#include "dec.h"
#include "display.h"
#include "events.h"
#include "misc/math.h"
#include "misc/mem.h"

static void print_error(const struct image_file *infile,
const char *what, const enum wu_error e, FILE *out) {
	fputs(what, out);
	fputs(": ", out);
	image_file_error_print(infile, e, out);
}

static enum wu_error decode_with_stats(struct wudec_image *image) {
	const watch_t start = watch_look();
	const enum wu_error result = wudec_decode(image);
	const watch_t diff = watch_elapsed(start);

	const struct image_file *infile = &image->file;
	const char *what = "Failed";
	if (result == wu_ok) {
		image_file_print(infile, stdout, 1, infile->nr == 1);
		what = "Decoded";
	} else {
		print_error(infile, "Decoding error", result, stdout);
	}
	nanosec_report(what, diff, report_normal);
	return result;
}

static void set_background_color(struct window_context *window,
const struct wudec_image *image) {
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
		gl_clear_color(&window->gl, bg);
	}
}

void display_end(struct window_context *window, const struct term_restore *tr) {
	gl_terminate(&window->gl);
	window_terminate(window);
	if (tr) {
		term_noncanon_end(tr);
	}
}

static enum wu_error update_texture(struct wudec_image *image,
struct gl_context *gl, const bool reset, const bool new_img,
const bool subupload) {
	struct wu_state *state = &image->state;
	struct wuimg *img = image->file.sub_img + state->idx;
	if (subupload) {
		return gl_subtexture_upload(gl, img, state)
			? wu_ok : wu_display_error;
	}
	const bool prev_was_scalable = gl->tex.no_transform & new_img;
	switch (gl_texture_upload(gl, img, image->conf.heed_pixel_ratio)) {
	case gl_upload_fail:
		term_line_put("Failed to upload to texture.", stderr);
		return wu_display_error;
	case gl_upload_success:
		if (reset | prev_was_scalable) {
			break;
		}
		return wu_ok;
	case gl_upload_same_size:
		if (prev_was_scalable) {
			break;
		}
		return wu_ok;
	}
	const float min = (float)(
		image->conf.magnify_under / (zumin(img->w, img->h) + 1) + 1
	);
	state->zoom = fminf(min, gl->tex.fit_zoom);
	state->rotate = 0;
	state->mirror = 0;
	state->x_offset = 0;
	state->y_offset = 0;
	return wu_ok;
}

static bool idle_display(struct wudec_image *image,
struct window_context *window, const double next_frame,
const enum image_event evs, const bool allow_cycle) {
	struct wu_event *event = &window->event;
	struct wu_state *state = &window->image.state;

	bool exit_display_loop = false;
	bool print_draw_time = true;
	do {
		print_draw_time &= window_draw(window);
		event->image = ev_none;
		int poll_msec;
		if (window->win.held_keys.active) {
			poll_msec = 1;
		} else if (isfinite(next_frame)) {
			poll_msec = (int)fmin(fmax(
				(next_frame - state->time)*1000.0, 0), INT_MAX);
		} else {
			poll_msec = -1;
		}
		window_poll(window, poll_msec);
		const unsigned char c = term_queue_next(&window->term);
		window_key_add(window, key_once, c, isupper(c));
		event_exec(window);
		if (window->win.playing) {
			const double elapsed = timespec_update(
				&window->win.timer.play);
			state->time += (float)elapsed;
			if (next_frame - state->time <= 0.001f
			&& wudec_frame_cycle(image, 1)) {
				event->image = ev_frame;
			} else if (evs & ev_time) {
				event->image = ev_time;
			}
		}
		window->gl.update |= (bool)event->image;

		if (print_draw_time) {
			nanosec_report("Drawn", gl_clock_query(&window->gl),
				report_all);
		}
		print_draw_time = !window->win.playing;

		event->cycle *= allow_cycle;
		exit_display_loop = event->exit | event->cycle
			| (event->rm == rm_yes);
	} while (!exit_display_loop & !(event->image & evs));
	return exit_display_loop;
}

static double next_frame_when(const struct wuimg *img) {
	struct image_anim *anim = img->anim;
	if (anim) {
		const struct frame_time time = anim->sec;
		const double secs = (double)time.num / (double)time.den;
		return fmax(secs, 1.0 / 30);
	}
	return 0.0;
}

enum wu_error display_loop(struct window_context *window,
const bool allow_cycle, const bool allow_delete) {
	struct wudec_image *image = &window->image;
	enum wu_error err = decode_with_stats(image);
	if (err != wu_ok) {
		return err;
	}

	struct image_file *infile = &image->file;
	struct wu_state *state = &window->image.state;
	struct wu_event *event = &window->event;

	*event = (struct wu_event){
		.rm = allow_delete ? rm_no : rm_never,
		.image = ev_subcycle, // for init only, not passed to image
	};

	set_background_color(window, image);
	window_set_title(window, infile->name);

	double next_frame = INFINITY;
	enum image_event evs = ev_subcycle;
	for (bool upload = true, first_iter = true; err == wu_ok;) {
		if (upload) {
			bool subupload = false;
			bool subcycle = false;
			if (event->image & ev_subcycle) {
				subcycle = true;
				state->time = 0;
				evs = wudec_cur_events(image);
				window->win.playing =
					wudec_cur_is_anim(image);

				if (!first_iter) {
					fputs(TERM_CLEAR_LINE, stdout);
					image_file_print_single(infile, stdout,
						1, (size_t)state->idx,
						TERM_TEMP_CLOSE);
					fflush(stdout);
				}
			} else {
				subupload = !(event->image & ev_transform);
			}

			struct gl_context *gl = &window->gl;
			err = update_texture(image, gl, first_iter, subcycle,
				subupload);
			if (err != wu_ok) {
				break;
			}

			first_iter = false;
			image_file_free_if_single(infile);
			if (evs & (ev_frame | ev_time)) {
				next_frame = state->time + next_frame_when(
					infile->sub_img + state->idx);
			} else {
				nanosec_report("Frame uploaded in",
					gl_clock_query(gl), report_all);
				next_frame = INFINITY;
			}
			if (subcycle) {
				timespec_present_time(&window->win.timer.play);
			} else if (window->win.playing) {
				state->time += timespec_update(&window->win.timer.play);
			}

			upload = false;
		}

		if (idle_display(image, window, next_frame, evs, allow_cycle)) {
			break;
		}

		err = wudec_callback(image, event->image);
		switch (err) {
		case wu_no_change:
			upload = event->image & (ev_subcycle | ev_frame);
			err = wu_ok;
			break;
		case wu_ok:
			upload = true;
			break;
		default:
			print_error(infile, "Callback error", err,
				stdout);
			break;
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

	term_line_key_val("Window backend", window->win.backend_name, stderr);

	const char *msg = gl_context_setup(&window->gl, &window->image.conf);
	if (msg) {
		window_terminate(window);
		term_line_key_val("Failed to configure OpenGL context", msg,
			stderr);
		return false;
	}

	window_postgl_setup(window);

	if (tr) {
		term_noncanon_start(tr);
	}
	watch_report("Display set", start, report_all);
	return true;
}

const char * display_offscreen_setup(struct window_offscreen *window,
struct gl_reader_context *reader, struct wu_conf *conf) {
	if (window_offscreen_setup(window)) {
		return gl_reader_init(reader, conf)
			? NULL
			: "Failed to set OpenGL renderer";
	}
	return "Failed to create window";
}

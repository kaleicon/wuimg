#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <math.h>
#include <limits.h>

#include "common.h"
#include "display.h"
#include "dec.h"
#include "events.h"

static void set_background_color(const struct image_context *image) {
	const struct image_file *infile = &image->file;
	const struct wu_conf *conf = &image->conf;

	if (conf->bg_src == bg_metadata
	&& !memchk(&infile->bg, 0, sizeof(infile->bg))) {
		uint8_t bg[4];
		memcpy(bg, &infile->bg, 3);
		bg[3] = conf->bg[3];
		gl_clear_color(bg);
	}
}

void display_end(struct window_context *window, const struct term_restore *tr) {
	window_terminate(window);
	gl_terminate(&window->pub.gl);
	if (tr) {
		term_noncanon_end(tr);
	}
}

static double poll_events(struct window_context *window) {
	unsigned char tk[32];
	const size_t read = term_event_read(tk, sizeof(tk));
	for (size_t i = 0; i < read; ++i) {
		window_key_add(&window->pub.held_keys, key_external,
			toupper(tk[i]), isupper(tk[i]));
	}
	window_poll(window);
	return event_exec(window);
}

static bool update_texture(struct image_context *image, struct gl_context *gl,
const bool reset, struct window_context *window) {
	struct wu_state *state = &image->state;
	struct raw_img *img = image->file.sub_img + state->idx;
	switch (gl_texture_upload(gl, img)) {
	case gl_upload_fail:
		puts("Failed to upload to texture.");
		return false;
	case gl_upload_success:
		(void)window;
		state->fit_zoom = gl_fit_zoom(gl, img->rotate);
		if (reset) {
			state->zoom = fminf(1, state->fit_zoom);
			state->rotate = 0;
			state->mirror = 0;
			state->x_offset = 0;
			state->y_offset = 0;
		}
		break;
	case gl_upload_same_size:
		break; // Keep state as it was
	}
	gl->update_matrix = true;

	if (!state->anim_playing) {
		printf("Frame %d uploaded in %lu ns\n",
			state->frame, gl_clock_query(gl));
	}
	return true;
}

static double idle_display(struct image_context *image,
struct window_context *window, double remaining) {
	struct wu_event *event = &window->pub.event;
	struct wu_state *state = &window->pub.image.state;
	struct gl_context *gl = &window->pub.gl;

	for (bool initial = true;; initial = false) {
		if (gl->update_matrix) {
			gl_matrix_update(gl, state);
			window_draw(window, false);
			if (!state->anim_playing) {
				printf("Drawn in %lu ns\n", gl_clock_query(gl));
			}
		}
		const struct timespec tm = {
			.tv_nsec = initial ? 2000000 : 5000000,
		};
		nanosleep(&tm, NULL);
		const double ellapsed = poll_events(window);

		if (window_has_focus(window) && state->anim_playing) {
			remaining -= ellapsed;
			if (remaining <= ellapsed) {
				event->image = image_frame_cycle(image, 1);
			}
		}

		if (event->cycle || event->program || event->rm == trit_true) {
			break;
		} else if (event->image) {
			gl->update_matrix = true;
			if (event->image & image->file.events
			|| event->image == ev_subcycle) {
				break;
			}
			event->image = 0;
		}
	}
	return remaining;
}

static double min_time(const struct raw_img *img, const struct wu_state *state) {
	struct image_frames *frames = img[state->idx].frames;
	if (frames) {
		return fmax(frames->f[state->frame].msec / 1000.0, 1.0 / 30);
	}
	return 0;
}

bool display_loop(struct window_context *window, const bool single_file) {
	struct image_context *image = &window->pub.image;
	struct image_file *infile = &image->file;
	struct wu_state *state = &window->pub.image.state;
	struct wu_event *event = &window->pub.event;

	*event = (struct wu_event){
		.image = ev_subcycle, // for init only, not passed to image
	};

	set_background_color(image);
	window_set_title(window, image->name);

	bool all_ok = true;
	double remaining = 0;
	for (bool upload = true, first_iter = true;;) {
		if (upload) {
			if (event->image == ev_subcycle) {
				state->anim_playing = raw_img_frames_nr(
					infile->sub_img + state->idx) > 1;
			}

			all_ok = update_texture(image, &window->pub.gl,
				first_iter, window);
			if (!all_ok) {
				break;
			}
			image_file_free_if_single(infile);

			if (state->anim_playing) {
				const double display_time = min_time(
					infile->sub_img, state);
				remaining = fmax(0, remaining + display_time);
			}
			upload = false;
			first_iter = false;
			event->image = 0;
		}

		remaining = idle_display(image, window, remaining);

		if ((!single_file && event->cycle)
		|| event->program || event->rm == trit_true) {
			break;
		} else if (event->image & infile->events) {
			const enum wu_error err = dec_callback_image(image,
				event->image);
			if (err == wu_ok) {
				upload = true;
			} else if (err != wu_no_change) {
				printf("Callback failed: %s\n",
					wu_error_message(err));
				all_ok = false;
				break;
			}
		}
		if (event->image & (ev_subcycle | ev_frame)) {
			upload = true;
		}
	}
	term_clear_line();
	return all_ok;
}

bool display_setup(struct window_context *window, struct term_restore *tr) {
	const clock_t start = clock();
	if (!window_setup(window)) {
		fputs("Failed to create window\n", stderr);
		return false;
	}

	printf("Window backend: %s\n", window_backend_name(window->backend));

	if (!gl_context_setup(&window->pub.gl, &window->pub.image.conf)) {
		window_terminate(window);
		fputs("Failed to configure OpenGL context\n", stderr);
		return false;
	}

	window_postgl_setup(window);

	if (tr) {
		term_noncanon_start(tr);
	}
	printf("Display set in %f seconds\n", clock_ellapsed(start));
	return true;
}

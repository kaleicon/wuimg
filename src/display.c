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
#include "raster/mem.h"

static void set_background_color(const struct image_context *image) {
	const struct image_file *infile = &image->file;
	const struct wu_conf *conf = &image->conf;

	uint8_t bg[4];
	if (conf->bg_src == bg_metadata
	&& memchk(&infile->bg, 0, sizeof(infile->bg))) {
		memcpy(bg, &infile->bg, sizeof(bg));
		bg[3] = conf->bg[3];
	} else {
		memcpy(bg, conf->bg, sizeof(bg));
	}
	gl_clear_color(bg);
}

void display_end(struct window_context *window, const struct term_restore *tr) {
	gl_terminate(&window->pub.gl);
	window_terminate(window);
	if (tr) {
		term_noncanon_end(tr);
	}
}

static double poll_events(struct window_context *window) {
	unsigned char tk[8];
	const size_t read = term_event_read(tk, sizeof(tk));
	for (size_t i = 0; i < read; ++i) {
		window_key_add(&window->pub.held_keys, key_external,
			toupper(tk[i]), isupper(tk[i]));
	}
	window_poll(window);
	return event_exec(window);
}

static bool update_texture(struct image_context *image, struct gl_context *gl,
const bool reset) {
	struct wu_state *state = &image->state;
	struct raw_img *img = image->file.sub_img + state->idx;
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
		break; // Keep state as it was
	}

	if (!state->anim_playing) {
		fprintf(stderr, "Frame %d uploaded in %lu ns\n",
			state->frame, gl_clock_query(gl));
	}
	return true;
}

static double idle_display(struct image_context *image,
struct window_context *window, double remaining) {
	struct wu_event *event = &window->pub.event;
	struct wu_state *state = &window->pub.image.state;
	struct gl_context *gl = &window->pub.gl;

	for (bool first = true;; first = false) {
		if (window_draw(window)) {
			if (!state->anim_playing && first) {
				fprintf(stderr, "Drawn in %lu ns\n",
					gl_clock_query(gl));
			}
		}
		const struct timespec tm = {
			.tv_nsec = 2000000,
		};
		nanosleep(&tm, NULL);
		const double ellapsed = poll_events(window);

		if (state->anim_playing && window_has_focus(window)) {
			remaining -= ellapsed;
			if (remaining <= ellapsed) {
				event->image = image_frame_cycle(image, 1);
			}
		}

		if (event->cycle || event->program || event->rm == trit_true) {
			break;
		} else if (event->image) {
			if (event->image & image->file.events
			|| event->image == ev_subcycle) {
				break;
			}
			gl->update = gl_update_matrix;
			event->image = 0;
		}
	}
	return remaining;
}

static double min_time(const struct raw_img *img, const struct wu_state *state) {
	struct image_frames *frames = img->frames;
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
			const struct raw_img *img = infile->sub_img + state->idx;
			if (event->image == ev_subcycle) {
				state->anim_playing = raw_img_frames_nr(img) > 1;
			}

			all_ok = update_texture(image, &window->pub.gl,
				first_iter);
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
	term_line_clear();
	return all_ok;
}

bool display_setup(struct window_context *window, struct term_restore *tr) {
	const clock_t start = clock();
	if (!window_setup(window)) {
		term_line_put("Failed to create window", stderr);
		return false;
	}

	fprintf(stderr, "Window backend: %s\n", window_backend_name(window->backend));

	if (!gl_context_setup(&window->pub.gl, &window->pub.image.conf)) {
		window_terminate(window);
		term_line_put("Failed to configure OpenGL context", stderr);
		return false;
	}

	window_postgl_setup(window);

	if (tr) {
		term_noncanon_start(tr);
	}
	clock_print("Display set", start);
	return true;
}

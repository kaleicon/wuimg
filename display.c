#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <math.h>
#include <limits.h>
#include <signal.h>

#include "wudefs.h"
#include "common.h"
#include "display.h"
#include "window.h"
#include "opengl.h"
#include "events.h"
#include "term.h"
#include "dec.h"
#include "colorimetry.h"

static volatile sig_atomic_t sig_should_close = 0;

static void signal_handler(int _signum) {
	(void)_signum;
	sig_should_close = 1;
}

static void set_background_color(const struct image_context *image) {
	const struct image_file *infile = &image->file;
	const struct wu_conf *conf = &image->conf;

	const float max = (float)UCHAR_MAX;
	float bg[4];
	for (size_t i = 0; i < sizeof(conf->bg); ++i) {
		bg[i] = conf->bg[i] / max;
	}

	switch (conf->bg_src) {
	case metadata:
		if (memchk(infile->bg, 0, sizeof(infile->bg))) {
			for (size_t i = 0; i < 3; ++i) {
				bg[i] = infile->bg[i] / max * bg[3];
			}
		}
		break;
	case average:
	case popular:
	case vibrant:
		get_image_color(bg, infile->sub_img, conf->bg_src, 64);
		for (size_t i = 0; i < 3; ++i) {
			bg[i] *= bg[3];
		}
		break;
	default:
		return;
	}
	gl_clear_color(bg);
}

void display_end(struct window_control *control,
const struct term_restore *tr) {
	gl_context_delete(&control->window.gl);
	window_terminate(&control->window);
	if (tr) {
		term_noncanon_end(tr);
	}
}

static void poll_events(struct image_context *image,
struct window_context *window, const double secs) {
	unsigned char tk[16];
	const size_t r = term_filter_read(tk);
	for (size_t i = 0; i < r; ++i) {
		event_add(key_external, toupper(tk[i]), isupper(tk[i]));
	}

	window_poll(window);
	event_exec(image, &window->event, secs);
	if (sig_should_close) {
		window->event.program = close_window;
	}
}

static bool update_texture(struct image_context *image, struct gl_context *gl,
const int idx, const bool reset_state) {
	const struct image_file *file = &image->file;
	const struct raw_img *img = file->sub_img + idx;

	struct wu_state *state = &image->state;
	state->dec_scale = img->dec_scale;

	if (state->anim != anim_playing) {
		gl_clock_start(gl);
	}

	if (gl_texture_reuse(img, gl)) {
		// pass
	} else if (gl_texture_upload(img, gl)) {
		gl_even_view(gl);
		if (reset_state) {
			state->rotate = img->rotate;
			state->mirror = img->mirror;
			state->x_offset = 0;
			state->y_offset = 0;
			state->fit_zoom = gl_fit_zoom(gl, state->rotate);
			state->zoom = fminf(1, state->fit_zoom);
		}
		gl_matrix_update(gl, state);
	} else {
		puts("Failed to load to texture.");
		return false;
	}

	if (state->anim != anim_playing) {
		printf("Sub-image %d uploaded in %lu nanoseconds.\n",
			idx, gl_clock_end(gl));
	}
	return true;
}

static double monoclock_diff(struct timespec *start) {
	struct timespec end;
	clock_gettime(CLOCK_MONOTONIC, &end);
	return (double)(end.tv_sec - start->tv_sec)
		+ (double)(end.tv_nsec - start->tv_nsec) / 1000000000;
}

static int monoclock_start(struct timespec *start) {
	return clock_gettime(CLOCK_MONOTONIC, start);
}

static double idle_display(struct image_context *image,
struct window_context *window, double remaining, struct timespec *start) {
	struct wu_state *state = &image->state;
	struct wu_event *event = &window->event;
	const enum image_event img_evs = image->file.events;

	for (;;) {
		window_draw(window);
		const double secs = monoclock_diff(start);
		monoclock_start(start);

		poll_events(image, window, secs);
		if (state->anim == anim_playing) {
			remaining -= secs;
			if (remaining <= 0.0) {
				state->sub.cycle = 1;
			}
		}

		if (state->sub.cycle || event->file.cycle
		|| event->program || event->rm == yes_rm) {
			break;
		} else if (event->window) {
			switch (event->window) {
			case toggle_fullscreen:
				window_toggle_fullscreen(window);
				break;
			case toggle_alpha:
				gl_alpha_state(&window->gl, state->alpha);
				break;
			}
			event->window = 0;
		}

		if (event->image) {
			gl_matrix_update(&window->gl, state);
			if (event->image & img_evs) {
				break;
			}
			event->image = 0;
		}
	}

	if (state->sub.cycle) {
		event->image |= sub_cycle;
	}
	return remaining;
}

static double min_time(const struct raw_img *img) {
	return fmax(1.0 / 30.0, (double)img->msec / 1000);
}

bool display_loop(struct window_control *control, const bool no_cycle) {
	struct image_context *image = &control->image;
	struct window_context *window = &control->window;
	int idx = 0;
	if (!update_texture(image, &window->gl, idx, true)) {
		return false;
	}

	set_background_color(image);
	window_set_title(window, image->name);

	const struct image_file *infile = &image->file;
	// No callbacks and only one image which has already been uploaded.
	if (!infile->events && !infile->dec_state && infile->nr == 1) {
		struct raw_img *img = infile->sub_img + idx;
		free(img->data);
		img->data = NULL;
	}

	struct wu_state *state = &image->state;
	state->sub = (struct wu_cycle){0};
	state->anim = infile->is_animation ? anim_playing : 0;
	window->event = (struct wu_event){0};

	bool all_ok = true;
	double remaining = min_time(infile->sub_img);
	for (;;) {
		struct timespec start;
		monoclock_start(&start);

		remaining = idle_display(image, window, remaining, &start);

		bool upload = false;
		if ((no_cycle == false && window->event.file.cycle)
		|| window->event.program || window->event.rm == yes_rm) {
			break;
		} else if (window->event.image) {
			const enum wu_error err = callback_image(image,
				window->event.image);
			if (err != wu_ok && err != wu_no_change) {
				printf("Callback failed: %s\n",
					wu_error_message(err));
				all_ok = false;
				break;
			}
			upload = (err == wu_ok);
			window->event.image = 0;
		}

		if (state->sub.cycle) {
			idx = imod(idx + state->sub.cycle, (int)infile->nr);
			state->sub.cycle = 0;
			upload = true;
		}

		if (upload) {
			all_ok = update_texture(image, &window->gl, idx, false);
			if (!all_ok) {
				break;
			}

			if (state->anim == anim_playing) {
				const double display_time = min_time(infile->sub_img + idx);
				remaining = fmax(0,
					remaining + display_time - monoclock_diff(&start));
			}
		}
	}

	if (infile->dec_state) {
		callback_image(image, 0);
	}

	term_clear_line();
	return all_ok;
}

bool display_setup(struct window_control *control, struct term_restore *tr) {
	if (tr) {
		term_noncanon_start(tr);
	}

	if (!window_setup(control)) {
		puts("Failed to create window.");
		return false;
	}

	if (!gl_context_setup(&control->window.gl, &control->image.conf)) {
		window_terminate(&control->window);
		puts("Failed to setup OpenGL context.");
		return false;
	}

	window_postgl_setup(control);

	const struct sigaction act = {
		.sa_handler = signal_handler,
		.sa_flags = (int)SA_RESETHAND,
	};
	sigaction(SIGABRT, &act, NULL);
	sigaction(SIGALRM, &act, NULL);
	sigaction(SIGINT, &act, NULL);
	sigaction(SIGPROF, &act, NULL);
	sigaction(SIGTERM, &act, NULL);
	sigaction(SIGVTALRM, &act, NULL);
	sigaction(SIGXCPU, &act, NULL);

	const struct sigaction ign = {
		.sa_handler = SIG_IGN,
	};
	sigaction(SIGHUP, &ign, NULL);
	return true;
}

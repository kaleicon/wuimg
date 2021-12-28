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
	case bg_metadata:
		if (memchk(&infile->bg, 0, sizeof(infile->bg))) {
			bg[0] = infile->bg.r / max * bg[3];
			bg[1] = infile->bg.g / max * bg[3];
			bg[2] = infile->bg.b / max * bg[3];
		}
		break;
	case bg_average:
	case bg_popular:
	case bg_vibrant:
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

void display_end(struct window_context *window, const struct term_restore *tr) {
	window_terminate(window);
	if (tr) {
		term_noncanon_end(tr);
	}
}

static double poll_events(struct window_context *window) {
	struct wu_keymap *held_keys = &window->pub.held_keys;

	unsigned char tk[32];
	const size_t read = term_event_read(tk, sizeof(tk));
	for (size_t i = 0; i < read; ++i) {
		event_add(held_keys, key_external, toupper(tk[i]), isupper(tk[i]));
	}

	const double ellapsed = window_poll(window);
	if (sig_should_close) {
		window->pub.event.program = close_window;
	}
	return ellapsed;
}

static bool update_texture(struct image_context *image, struct gl_context *gl,
const int idx, const bool reset_state) {
	const struct raw_img *img = image->file.sub_img + idx;
	struct wu_state *state = &image->state;
	if (state->anim != anim_playing) {
		gl_clock_start(gl);
	}

	switch (gl_texture_upload(gl, img)) {
	case gl_upload_fail:
		puts("Failed to upload to texture.");
		return false;
	case gl_upload_success:
		state->fit_zoom = gl_fit_zoom(gl, state->rotate);
		const float fit_screen = fminf(1, state->fit_zoom);
		if (reset_state) {
			state->rotate = img->rotate;
			state->mirror = img->mirror;
			state->x_offset = 0;
			state->y_offset = 0;
			state->zoom = fit_screen;
		} else if (state->zoom < fit_screen) {
			state->zoom = fit_screen;
		}
		break;
	case gl_upload_reused:
		break; // Keep the texture as it was
	}
	gl->update_matrix = true;

	if (state->anim != anim_playing) {
		printf("Sub-image %d uploaded in %lu nanoseconds.\n",
			idx, gl_clock_end(gl));
	}
	return true;
}

static double idle_display(struct image_context *image,
struct window_context *window, double remaining) {
	struct wu_event *event = &window->pub.event;
	struct wu_state *state = &window->pub.image.state;
	struct gl_context *gl = &window->pub.gl;

	gl->update_matrix = true;
	for (;;) {
		if (gl->update_matrix) {
			gl_matrix_update(gl, state);
		}
		const double ellapsed = poll_events(window);
		window_draw(window);

		if (window_has_focus(window) && state->anim == anim_playing) {
			remaining -= ellapsed;
			if (remaining <= 0.0) {
				state->cycle += 1;
				event->image = ev_subcycle;
			}
		}

		if (state->cycle) {
			state->idx = imod(state->idx + state->cycle,
				(int)image->file.nr);
			if (remaining > 0) { // Manual cycling
				state->anim |= 1;
			}
			break;
		} else if (event->cycle || event->program
		|| event->rm == yes_rm) {
			break;
		}

		if (event->image) {
			gl->update_matrix = true;
			if (event->image & image->file.events) {
				break;
			}
			event->image = 0;
		}
	}
	return remaining;
}

static double min_time(const struct raw_img *img) {
	return fmax(1.0 / 30.0, (double)img->msec / 1000);
}

bool display_loop(struct window_context *window, const bool no_cycle) {
	struct image_context *image = &window->pub.image;

	const struct image_file *infile = &image->file;

	struct wu_state *state = &window->pub.image.state;
	struct wu_event *event = &window->pub.event;

	state->idx = 0;
	state->cycle = 0;
	state->anim = infile->is_animation ? anim_playing : 0;
	*event = (struct wu_event){0};

	if (!update_texture(image, &window->pub.gl, state->idx, true)) {
		return false;
	}

	set_background_color(image);
	window_set_title(window, image->name);

	// No callbacks and only one image that has already been uploaded.
	if (!infile->events && !infile->dec_state && infile->nr == 1) {
		struct raw_img *img = infile->sub_img;
		free(img->data);
		img->data = NULL;
	}

	bool all_ok = true;
	double remaining = min_time(infile->sub_img);
	for (;;) {
		remaining = idle_display(image, window, remaining);

		bool upload = false;
		if ((no_cycle == false && event->cycle)
		|| event->program || event->rm == yes_rm) {
			break;
		} else if (event->image) {
			const enum wu_error err = callback_image(image,
				event->image);
			if (err == wu_ok) {
				upload = true;
			} else if (err != wu_no_change) {
				printf("Callback failed: %s\n",
					wu_error_message(err));
				all_ok = false;
				break;
			}
			event->image = 0;
		}

		if (state->cycle) {
			state->cycle = 0;
			upload = true;
		}

		if (upload) {
			all_ok = update_texture(image, &window->pub.gl,
				state->idx, false);
			if (!all_ok) {
				break;
			}

			if (state->anim == anim_playing) {
				const double display_time = min_time(
					infile->sub_img + state->idx);
				remaining = fmax(0, remaining + display_time);
			}
		}
	}

	if (infile->dec_state) {
		callback_image(image, ev_end);
	}

	term_clear_line();
	return all_ok;
}

bool display_setup(struct window_context *window, struct term_restore *tr) {
	const clock_t start = clock();
	if (!window_setup(window)) {
		fputs("Failed to create window.\n", stderr);
		return false;
	}

	switch (window->backend) {
	case window_glfw: puts("Using GLFW"); break;
	case window_drm: puts("Using DRM"); break;
	case window_wayland: puts("Using Wayland"); break;
	}

	if (!gl_context_setup(&window->pub.gl, &window->pub.image.conf)) {
		window_terminate(window);
		fputs("Failed to setup OpenGL context.\n", stderr);
		return false;
	}

	window_postgl_setup(window);

	if (tr) {
		term_noncanon_start(tr);
	}

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

	printf("Display set in %f seconds\n", clock_ellapsed(start));
	return true;
}

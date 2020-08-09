#include <stdio.h>
#include <ctype.h>
#include <time.h>
#include <math.h>
#include <termios.h>
#include <unistd.h>
#include <limits.h>

#include "wudefs.h"
#include "common.h"
#include "display.h"
#include "window.h"
#include "opengl.h"
#include "events.h"
#include "dec.h"
#include "colorimetry.h"

enum update_tex {
	first_load = 1,
	callback = 2,
};

static float millif(const struct timespec *restrict before,
const struct timespec *restrict after) {
	return (float)timespec_nanodiff(*before, *after) / 1000000;
}

static void set_background_color(const struct wu_conf *wuconf,
const struct image_file *infile) {
	if (!wuconf->bg[3]) { // Always fully transparent
		return;
	}

	const float max = (float)UCHAR_MAX;
	float bg[4];
	for (size_t i = 0; i < sizeof(wuconf->bg); ++i) {
		bg[i] = wuconf->bg[i] / max;
	}

	switch (wuconf->bg_src) {
	case metadata:
		;const unsigned char *ibg = infile->bg;
		if (ibg[0] || ibg[1] || ibg[2] || ibg[3]) {
			for (size_t i = 0; i < 3; ++i) {
				bg[i] = ibg[i] / max;
			}
		}
		break;
	case average:
	case popular:
	case vibrant:
		get_image_color(bg, infile->sub_img, wuconf->bg_src, 64);
		for (size_t i = 0; i < 3; ++i) {
			bg[i] *= bg[3];
		}
		break;
	default:
		return;
	}
	clear_gl_color(bg);
}

void end_display(const struct window_control *control,
const struct term_restore *tr) {
	delete_gl_context(&control->context);
	terminate_window();
	if (tr) {
		struct termios term;
		tcgetattr(STDIN_FILENO, &term);

		term.c_lflag = tr->lflag;
		term.c_cc[VMIN] = tr->vmin;
		term.c_cc[VTIME] = tr->vtime;
		tcsetattr(STDIN_FILENO, TCSANOW, &term);
	}
}

static void term_read(void) {
	unsigned char ch[32];
	ssize_t r = read(STDIN_FILENO, ch, sizeof(ch));
	for (ssize_t i = 0; i < r && isprint(ch[i]); ++i) {
		add_event(key_external, ch[i]);
	}
}

static void poll_events(struct window_control *control, const float msecs) {
	term_read();
	poll_window(control);
	exec_events(&control->state, &control->event, &control->file, msecs);
}

static bool update_window(struct window_control *control,
const struct raw_img *img, const bool reset_state) {
	struct gl_context *context = &control->context;
	if (reuse_gl_texture(img, context)) {
		return true;
	} else if (load_gl_texture(img, context)) {
		even_gl_view(context);
		struct wu_state *state = &control->state;
		if (reset_state) {
			state->x_offset = 0;
			state->y_offset = 0;
			state->rotate = img->rotate;
			state->mirror = img->mirror;
		}

		state->fit_zoom = calc_gl_fit_zoom(context, state->rotate);
		if (reset_state) {
			state->zoom = fminf(1, state->fit_zoom);
		}
		update_gl_matrix(context, state);
		return true;
	}
	return false;
}

static float idle_display(struct window_control *control, float remaining,
struct timespec *restrict before, struct timespec *restrict after,
const enum image_event img_ev) {
	struct wu_event *event = &control->event;
	struct wu_state *state = &control->state;

	clock_gettime(CLOCK_REALTIME, before);
	for (;;) {
		redraw_window(control->window);
		clock_gettime(CLOCK_REALTIME, after);
		const float msecs = millif(before, after);
		clock_gettime(CLOCK_REALTIME, before);

		poll_events(control, msecs);
		if (state->anim == playing && control->screen.has_focus) {
			remaining -= msecs;
			if (remaining <= 0.0f) {
				state->sub.cycle = 1;
			}
		}

		if (event->image) {
			if (event->image & img_ev) {
				break;
			}
			update_gl_matrix(&control->context, state);
			event->image = 0;
		}

		if (state->sub.cycle || control->file.cycle
		|| event->program || event->rm == yes_rm) {
			break;
		} else if (event->window == toggle_fullscreen) {
			set_fullscreen_window(control);
			event->window = 0;
		}
	}

	if (state->sub.cycle) {
		event->image |= sub_cycle;
	}
	return remaining;
}

bool display_loop(struct window_control *control, struct image_file *infile,
const char *filename, const bool no_cycle) {
	set_window_title(control->window, filename);

	control->file = (struct wu_pos){0};
	control->event = (struct wu_event){0};

	struct wu_state *state = &control->state;
	state->sub = (struct wu_pos){0};
	state->anim = infile->is_animation ? playing : 0;

	int idx = 0;
	float remaining = (float)infile->sub_img->msec;
	enum update_tex upload = first_load;
	bool all_ok = true;

	struct timespec before, after;
	clock_gettime(CLOCK_REALTIME, &before);
	for (;;) {
		struct raw_img *img = infile->sub_img + idx;
		if (upload || state->sub.cycle) {
			if (!update_window(control, img, upload == first_load)) {
				printf("Failed to load %s to texture.\n",
					filename);
				all_ok = false;
				break;
			}

			if (upload == first_load) {
				set_background_color(&control->conf, infile);
			}

			if (state->anim != playing) {
				clock_gettime(CLOCK_REALTIME, &after);
				printf("Sub-image %d uploaded in %ld "
					"nanoseconds.\n", idx,
					timespec_nanodiff(before, after));
			}

			if (!infile->events && !infile->dec_state) {
				if (img->data && infile->nr == 1) {
					free(img->data);
					img->data = NULL;
				}
			}
		}

		if (state->anim == playing && upload != first_load) {
			clock_gettime(CLOCK_REALTIME, &after);
			const int min = 1000 / 30;
			const float display_time = (float)imax(img->msec, min);
			remaining += display_time - millif(&before, &after);
			remaining = fmaxf(remaining, 0);
		}
		state->sub.cycle = 0;
		upload = false;

		remaining = idle_display(control, remaining, &before, &after,
			infile->events);

		if ((!no_cycle && control->file.cycle)
		|| control->event.program || control->event.rm == yes_rm) {
			break;
		}

		const enum image_event evs = infile->events & control->event.image;
		if (evs) {
			const enum wu_error err = callback_image(infile,
				&control->conf, state, evs);
			if (err != wu_ok) {
				printf("Callback failed with code %u: %s\n",
					err, wu_error_message(err));
				all_ok = false;
				break;
			}
			upload = callback;
		}
		control->event.image = 0;
		idx = iwrap(idx + state->sub.cycle, (int)infile->nr);
	}

	if (infile->dec_state) {
		callback_image(infile, &control->conf, state, 0);
	}

	fputs(CLEAR_LINE, stdout);
	return all_ok;
}

static void setup_terminal(struct term_restore *tr) {
	struct termios term;
	tcgetattr(STDIN_FILENO, &term);
	*tr = (struct term_restore) {
		.lflag = term.c_lflag,
		.vmin = term.c_cc[VMIN],
		.vtime = term.c_cc[VTIME],
	};

	term.c_lflag &= (tcflag_t)~(ECHO | ICANON);
	term.c_cc[VMIN] = 0;
	term.c_cc[VTIME] = 0;
	tcsetattr(STDIN_FILENO, TCSANOW, &term);
}

bool setup_display(struct window_control *control, struct term_restore *tr) {
	if (tr) {
		setup_terminal(tr);
	}

	if (!setup_window(control)) {
		puts("Failed to create window.");
		return false;
	}

	if (!setup_opengl(&control->context, &control->conf)) {
		terminate_window();
		puts("Failed to setup OpenGL context.");
		return false;
	}
	return true;
}

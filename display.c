#include <stdio.h>
#include <string.h>
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

static float millif(const struct timespec *start) {
	return (float)clock_nanodiff(start) / 1000000;
}

static void set_background_color(const struct wu_conf *wuconf,
const struct image_file *infile) {
	const float max = (float)UCHAR_MAX;
	float bg[4];
	for (size_t i = 0; i < sizeof(wuconf->bg); ++i) {
		bg[i] = wuconf->bg[i] / max;
	}

	switch (wuconf->bg_src) {
	case metadata:
		;
		const unsigned char null[sizeof(infile->bg)] = {0};
		if (memcmp(infile->bg, null, sizeof(infile->bg))) {
			for (size_t i = 0; i < 3; ++i) {
				bg[i] = infile->bg[i] / max * bg[3];
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

void end_display(const struct term_restore *tr) {
	delete_gl_context();
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
	const unsigned char escbuf[2] = {0x1b, '['};
	unsigned char ch[8] = {0};
	const ssize_t r = read(STDIN_FILENO, ch, sizeof(ch));
	if (r > 2 && !memcmp(escbuf, ch, sizeof(escbuf))) {
		size_t idx = 2;
		bool shift = false;

		const unsigned char shift_mod[] = {'1', ';', '2'};
		if (!memcmp(ch + idx, shift_mod, sizeof(shift_mod))) {
			shift = true;
			idx += sizeof(shift_mod);
		}

		unsigned char c;
		switch (ch[idx]) {
		case 'A': c = 'K'; break;
		case 'B': c = 'J'; break;
		case 'C': c = 'L'; break;
		case 'D': c = 'H'; break;
		case 'F': c = '1'; break;
		case 'H': c = '0'; break;
		default: return;
		}
		add_event(key_external, c, shift);
	} else if (isprint(ch[0])) {
		add_event(key_external, (unsigned char)toupper(ch[0]),
			isupper(ch[0]));
	}
}

static void poll_events(struct window_control *control, const float msecs) {
	term_read();
	poll_window(control);
	exec_events(control->file, &control->state, &control->event, msecs);
}

static bool update_window(struct window_control *control,
const struct image_file *infile, const int idx, const char *filename,
const bool reset_state) {
	struct timespec start;
	clock_start(&start);

	const struct raw_img *img = infile->sub_img + idx;
	struct gl_context *context = &control->context;
	struct wu_state *state = &control->state;
	state->dec_scale = img->dec_scale;

	if (reuse_gl_texture(img, context)) {
	} else if (load_gl_texture(img, context)) {
		even_gl_view(context);
		if (reset_state) {
			state->anim = infile->is_animation ? playing : 0;
			state->rotate = img->rotate;
			state->mirror = img->mirror;
			state->x_offset = 0;
			state->y_offset = 0;
			state->zoom = fminf(1,
				calc_gl_fit_zoom(context, state->rotate));
		}
		update_gl_matrix(context, state);
	} else {
		printf("Failed to load %s to texture.\n", filename);
		return false;
	}

	if (state->anim != playing) {
		printf("Sub-image %d uploaded in %ld nanoseconds.\n",
			idx, clock_nanodiff(&start));
	}
	return true;
}

static float idle_display(struct window_control *control, float remaining,
struct timespec *start, const enum image_event img_ev) {
	struct wu_event *event = &control->event;
	struct wu_state *state = &control->state;

	clock_start(start);
	for (;;) {
		redraw_window(control->window);
		const float msecs = millif(start);
		clock_start(start);

		poll_events(control, msecs);
		if (state->anim == playing && control->geom.has_focus) {
			remaining -= msecs;
			if (remaining <= 0.0f) {
				state->sub.cycle = 1;
			}
		}

		if (event->image) {
			update_gl_matrix(&control->context, state);
			if (event->image & img_ev) {
				break;
			}
			event->image = 0;
		}

		if (state->sub.cycle || event->file.cycle
		|| event->program || event->rm == yes_rm) {
			break;
		} else if (event->window) {
			switch (event->window) {
			case toggle_fullscreen:
				set_fullscreen_window(control);
				break;
			case toggle_alpha:
				set_gl_alpha(&control->context, state->alpha);
				break;
			}
			event->window = 0;
		}
	}

	if (state->sub.cycle) {
		event->image |= sub_cycle;
	}
	return remaining;
}

bool display_loop(struct image_file *infile, struct window_control *control,
const char *filename, const bool no_cycle) {
	int idx = 0;
	if (!update_window(control, infile, idx, filename, true)) {
		return false;
	}

	set_background_color(&control->conf, infile);
	set_window_title(control->window, filename);

	if (!infile->events && !infile->dec_state && infile->nr == 1) {
		struct raw_img *img = infile->sub_img + idx;
		free(img->data);
		img->data = NULL;
	}

	control->file = infile;
	control->event = (struct wu_event){0};

	struct wu_state *state = &control->state;
	state->sub = (struct wu_cycle){0};

	bool all_ok = true;

	struct timespec start;
	bool upload = false;
	float remaining = (float)infile->sub_img->msec;
	for (;;) {
		remaining = idle_display(control, remaining, &start,
			infile->events);
		if ((no_cycle == false && control->event.file.cycle)
		|| control->event.program || control->event.rm == yes_rm) {
			break;
		}

		const enum image_event ev = infile->events & control->event.image;
		if (ev) {
			const enum wu_error err = callback_image(infile,
				&control->conf, state, ev);
			if (err != wu_ok && err != wu_no_change) {
				printf("Callback failed with code %d: %s\n",
					err, wu_error_message(err));
				all_ok = false;
				break;
			}
			upload = (err == wu_ok);
		}
		control->event.image = 0;
		if (state->sub.cycle && state->sub.cycle % (int)infile->nr) {
			idx = imod(idx + state->sub.cycle, (int)infile->nr);
			upload = true;
		}
		state->sub.cycle = 0;

		if (upload) {
			all_ok = update_window(control, infile, idx, filename,
				false);
			if (!all_ok) {
				break;
			}

			if (state->anim == playing) {
				struct raw_img *img = infile->sub_img + idx;
				const int min = 1000 / 30;
				const float display_time = (float)imax(
					img->msec, min);
				remaining += display_time
					- millif(&start);
				remaining = fmaxf(0, remaining);
			}
			upload = false;
		}
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

	poll_window(control);
	return true;
}

// SPDX-License-Identifier: 0BSD
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "misc/math.h"
#include "events.h"
#include "term.h"
#include "write_pam.h"
#include "wudefs.h"

enum event_repeat {
	repeat_none = 0,
	repeat_fixed = 1,
	repeat_smooth = 2,
};

static unsigned char * get_map(struct window_keymap *held_keys) {
	return held_keys->map - WINDOW_KEYSTART;
}

static enum event_repeat apply_event(struct window_context *window,
const int code, const float dt, const bool shift) {
	struct window_public *pub = &window->pub;
	struct image_context *image = &pub->image;
	const struct image_file *file = &image->file;
	struct wu_state *state = &image->state;
	struct wu_event *event = &pub->event;
	struct gl_context *gl = &pub->gl;

	switch (code) {
	// Exit
	case 'Q':
		event->program = wu_program_exit;
		break;
	// Reload file
	case 'R':
		event->program = wu_program_reload_file;
		break;

	// Fullscreen
	case 'F':
		window_fullscreen(window);
		return repeat_none;
	// Alpha display
	case 'A':
		gl_alpha_toggle(gl, shift ? -1 : 1);
		break;
	// Metadata
	case 'M':
		image_file_print(file, 2 + shift);
		return repeat_none;

	// Delete
	case 'D':
		if (!shift && event->rm == trit_false) {
			event->rm = trit_what;
			term_line_temp("Delete file?"
				" (D to confirm, u to dismiss)");
		} else if (shift && event->rm == trit_what) {
			event->rm = trit_true;
		}
		return repeat_none;
	// Abort delete
	case 'U':
		event->program = 0;
		term_line_clear();
		return repeat_none;

	// Cycling
	case 'N': // Next
		event->cycle += shift ? 10 : 1;
		break;
	case 'P': // Prev
		event->cycle -= shift ? 10 : 1;
		break;
	// Sub-cycling
	case '<': // Prev
		event->image = image_sub_cycle(image, -1);
		break;
	case '>': // Next
		event->image = image_sub_cycle(image, 1);
		break;
	// Frame cycling
	case ',': // Prev
		event->image = image_frame_cycle(image, -1);
		state->anim_playing = false;
		break;
	case '.': // Next
		event->image = image_frame_cycle(image, 1);
		state->anim_playing = false;
		break;
	case ';':
		event->image = image_sub_cycle(image, -5);
		state->anim_playing = false;
		break;
	case ':':
		event->image = image_sub_cycle(image, 5);
		state->anim_playing = false;
		break;
	case ' ':
		if (wuimg_frames_nr(image_cur_sub_img(image)) > 1) {
			state->anim_playing = !state->anim_playing;
		} else {
			state->anim_playing = false;
		}
		return repeat_none;

	// Image movement
	case 'H': // Left
		event->image = ev_move;
		state->x_offset += dt / state->zoom;
		return repeat_smooth;
	case 'J': // Down
		event->image = ev_move;
		state->y_offset -= dt / state->zoom;
		return repeat_smooth;
	case 'K': // Up
		event->image = ev_move;
		state->y_offset += dt / state->zoom;
		return repeat_smooth;
	case 'L': // Right
		event->image = ev_move;
		state->x_offset -= dt / state->zoom;
		return repeat_smooth;

	// Rotation
	case 'Z': // Counterclockwise
		event->image = ev_mirrot;
		state->rotate = (state->rotate - 1) & 3;
		break;
	case 'X': // Clockwise
		event->image = ev_mirrot;
		state->rotate = (state->rotate + 1) & 3;
		break;

	// Mirror
	case 'I': // Horizontal
		event->image = ev_mirrot;
		state->mirror = !state->mirror;
		state->rotate = (state->rotate + 2) & 0x03;
		break;
	case 'O': // Vertical
		event->image = ev_mirrot;
		state->mirror = !state->mirror;
		break;

	// Zoom
	case '+':
		event->image = image_zoom(image, state->zoom * exp2f(1.0f/3.0f));
		break;
	case '-':
		event->image = image_zoom(image, state->zoom * exp2f(1.0f/-3.0f));
		break;
	case '*':
		event->image = image_zoom(image, state->zoom * exp2f(1.0f/6.0f));
		break;
	case '/':
		event->image = image_zoom(image, state->zoom * exp2f(1.0f/-6.0f));
		break;
	case '=':
	case '0':
		state->x_offset = 0;
		state->y_offset = 0;
		const float fit = gl->tex.fit_zoom;
		event->image = image_zoom(image,
			(code == '0') ? fminf(1.0, fit) : fit);
		event->image |= ev_move;
		return repeat_none;
	case '1': case '2': case '3': case '4':
	case '5': case '6': case '7': case '8': case '9':
		;const struct wuimg *img = image_cur_sub_img(image);
		event->image = image_zoom(image,
			(float)(code - '0') * (1/img->dec_scale));
		return repeat_none;
	}
	return repeat_fixed;
}

static double key_events(struct window_context *window, const double secs) {
	struct window_keymap *held_keys = &window->pub.held_keys;
	const bool shift = held_keys->shift;

	float msecs = (float)(secs * 1000);
	const int inc = (int)msecs;
	if (shift) {
		msecs *= 2;
	}

	unsigned char *map = get_map(held_keys);
	for (int key = WINDOW_KEYSTART; key < WINDOW_KEYEND; ++key) {
		const unsigned char time = map[key];
		float dt = 16 * (shift ? 2 : 1);
		switch (time) {
		case 0:
			continue;
		case key_external:
			map[key] = 0;
			break;
		case 0xff:
			dt = msecs;
			break;
		default:
			map[key] = (unsigned char)imin(0xff, time + inc);
			if (time != key_press) {
				continue;
			}
		}

		switch (apply_event(window, key, dt, shift)) {
		case repeat_none:
			map[key] = 0;
			break;
		case repeat_fixed:
//			if (map[key] == 255) {
//				map[key] = 255 - 7;
//			}
			break;
		case repeat_smooth: break;
		}
	}
	return secs;
}

static double monoclock_diff(const struct timespec start,
const struct timespec end) {
	const double nanos_per_sec = 1000000000;
	return (double)(end.tv_sec - start.tv_sec)
		+ (double)(end.tv_nsec - start.tv_nsec) / nanos_per_sec;
}

double event_exec(struct window_context *window) {
	struct window_public *pub = &window->pub;
	struct window_cursor *cursor = &pub->win.cur;
	const int x_scroll = iclamp((int)cursor->x.scroll, -1, 1);
	if (x_scroll) {
		pub->event.image = image_sub_cycle(&pub->image, x_scroll);
		cursor->x.scroll = 0;
	}
	const int y_scroll = iclamp((int)cursor->y.scroll, -1, 1);
	if (y_scroll) {
		pub->event.cycle = y_scroll;
		cursor->y.scroll = 0;
	}

	const struct timespec start = pub->timer;
	clock_gettime(CLOCK_MONOTONIC, &pub->timer);
	return key_events(window, fmax(1.0/1000, monoclock_diff(start, pub->timer)));
}

void print_keys(void) {
	puts("Keybinds (case insensitive except where noted):\n"
		"\tq | Alt+F4 | Ctrl+w\n"
		"\t\tQuit.\n"

		"\tf | F11\n"
		"\t\tToggle fullscreen.\n"

		"\ta | A\n"
		"\t\tCycle forwards or backwards between alpha blending modes.\n"

		"\tm | M\n"
		"\t\tPrint unabreviatted metadata. For 'm', display the full\n"
		"\t\thierarchy but omit fields that would occupy more than a\n"
		"\t\tline or two of text. For 'M', omit nothing.\n"

		"\tr\n"
		"\t\tReload current file.\n"

		"\td\n"
		"\t\tPrompt to delete the current file. 'D' to confirm, 'u'\n"
		"\t\tto dismiss.\n"

		"\tn | p | N | P\n"
		"\t\tGo to the next or previous file. If uppercase, skip 10\n"
		"\t\timages at a time.\n"

		"\t< | >\n"
		"\t\tGo to the previous or next sub-image.\n"

		"\t, | . | ; | :\n"
		"\t\tFor comma and period, go to the previous or next frame\n"
		"\t\twithin an animated sub-image. For colons, skip 5 frames\n"
		"\t\tat a time. Note that seeking backwards can be slow.\n"

		"\th | j | k | l | H | J | K | L | Arrow keys\n"
		"\t\tMove viewport to the left, down, up, and right,\n"
		"\t\trespectively. If uppercase (or shift is held), move\n"
		"\t\ttwice as much.\n"

		"\tz | x\n"
		"\t\tRotate counter- or clockwise.\n"

		"\ti | o\n"
		"\t\tMirror horizontally or vertically.\n"

		"\t+ | - | PageUp | PageDown\n"
		"\t\tZoom in or out. The image size is doubled or halved every\n"
		"\t\tthree presses.\n"

		"\t* | / | PageUp+Shift | PageDown+Shift\n"
		"\t\tLike + and -, but the size is doubled or halved every\n"
		"\t\tsix presses.\n"

		"\t0 | Home\n"
		"\t\tCenter image, and fit to window or scale to 1x, whichever\n"
		"\t\tis smaller.\n"

		"\t= | End\n"
		"\t\tCenter and fit to window.\n"

		"\t1 | Home+Shift\n"
		"\t\t1x zoom.\n"

		"\t2 .. 9\n"
		"\t\t[n]x zoom.");
}

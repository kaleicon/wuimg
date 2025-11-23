// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2020 kaleido
#include "events.h"
#include "misc/math.h"
#include "misc/term.h"

enum event_repeat {
	repeat_none = 0,
	repeat_fixed = 1,
	repeat_smooth = 2,
};

static enum event_repeat apply_event(struct window_context *window,
const int code, const float dt, const bool shift) {
	struct window_public *pub = &window->pub;
	struct wudec_image *image = &pub->image;
	const struct image_file *file = &image->file;
	struct wu_state *state = &image->state;
	struct wu_event *event = &pub->event;
	struct gl_context *gl = &pub->gl;

	switch (code) {
	// Exit
	case 'Q':
		event->exit = true;
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
		image_file_print(file, stdout, 2 + shift, file->nr == 1);
		return repeat_none;

	// Delete
	case 'D':
		if (!shift && event->rm == rm_no) {
			event->rm = rm_ask;
			term_line_temp("Delete file?"
				" (D to confirm, u to dismiss)");
		} else if (shift && event->rm == rm_ask) {
			event->rm = rm_yes;
		}
		return repeat_none;
	// Abort delete
	case 'U':
		if (event->rm == rm_ask) {
			event->rm = rm_no;
		}
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
		event->image = wudec_sub_cycle(image, -1);
		break;
	case '>': // Next
		event->image = wudec_sub_cycle(image, 1);
		break;
	// Frame cycling
	case ',': // Prev
		event->image = wudec_frame_cycle(image, -1);
		pub->win.playing = false;
		break;
	case '.': // Next
		event->image = wudec_frame_cycle(image, 1);
		pub->win.playing = false;
		break;
	case ';':
		event->image = wudec_sub_cycle(image, -5);
		pub->win.playing = false;
		break;
	case ':':
		event->image = wudec_sub_cycle(image, 5);
		pub->win.playing = false;
		break;
	case ' ':
		pub->win.playing ^= wudec_cur_is_anim(image);
		return repeat_none;

	// Image movement
	case 'H': // Left
		event->image = ev_transform;
		state->x_offset += dt / state->zoom;
		return repeat_smooth;
	case 'J': // Down
		event->image = ev_transform;
		state->y_offset -= dt / state->zoom;
		return repeat_smooth;
	case 'K': // Up
		event->image = ev_transform;
		state->y_offset += dt / state->zoom;
		return repeat_smooth;
	case 'L': // Right
		event->image = ev_transform;
		state->x_offset -= dt / state->zoom;
		return repeat_smooth;

	// Rotation
	case 'Z': // Counterclockwise
		event->image = ev_transform;
		state->rotate = (state->rotate - 1) & 0x03;
		break;
	case 'X': // Clockwise
		event->image = ev_transform;
		state->rotate = (state->rotate + 1) & 0x03;
		break;

	// Mirror
	case 'I': // Horizontal
		event->image = ev_transform;
		state->mirror = !state->mirror;
		state->rotate = (state->rotate + 2) & 0x03;
		break;
	case 'O': // Vertical
		event->image = ev_transform;
		state->mirror = !state->mirror;
		break;

	// Zoom
	case '+':
		event->image = wudec_zoom(image, state->zoom * exp2f(1.0f/3.0f));
		break;
	case '-':
		event->image = wudec_zoom(image, state->zoom * exp2f(1.0f/-3.0f));
		break;
	case '*':
		event->image = wudec_zoom(image, state->zoom * exp2f(1.0f/6.0f));
		break;
	case '/':
		event->image = wudec_zoom(image, state->zoom * exp2f(1.0f/-6.0f));
		break;
	case '=':
	case '0':
		;const float fit = gl->tex.fit_zoom;
		event->image = wudec_zoom(image,
			(code == '0') ? fminf(1.0, fit) : fit);
		event->image |= ((bool)state->x_offset | (bool)state->y_offset)
			? ev_transform : ev_none;
		state->x_offset = 0;
		state->y_offset = 0;
		return repeat_none;
	case '1': case '2': case '3': case '4':
	case '5': case '6': case '7': case '8': case '9':
		event->image = wudec_zoom(image, (float)(code - '0'));
		return repeat_none;
	}
	return repeat_fixed;
}

double event_exec(struct window_context *window) {
	struct window_keymap *held_keys = &window->pub.held_keys;
	const bool shift = held_keys->shift;

	const double elapsed = window_timer_update(&window->pub);
	float msecs = (float)(fmax(elapsed, 1.0/1000) * 1000);
	const int inc = (int)msecs;
	if (shift) {
		msecs *= 2;
	}

	unsigned char *map = window_keymap_map(held_keys);
	for (int key = WINDOW_KEYSTART; key < WINDOW_KEYEND; ++key) {
		const unsigned char time = map[key];
		float dt = (float)(16 << shift);
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
			break;
		case repeat_smooth: break;
		}
	}
	return elapsed;
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

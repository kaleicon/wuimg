// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2020 kaleido
#include "events.h"
#include "misc/math.h"
#include "misc/term.h"

static void sub_cycle(struct window_context *window, const int n) {
	window->event.image = wudec_sub_cycle(&window->image, n);
	window->win.playing &= !window->event.image;
}

static void frame_cycle(struct window_context *window, const int n) {
	window->event.image = wudec_frame_cycle(&window->image, n);
	window->win.playing = false;
	const int frame = window->image.state.frame;
	char msg[sizeof(frame)*3 + 9];
	snprintf(msg, sizeof(msg), "Frame: %i", frame);
	term_line_temp(msg);
}

static bool apply_event(struct window_context *window, const int code,
const float dt, const bool shift) {
	struct wudec_image *image = &window->image;
	const struct image_file *file = &image->file;
	struct wu_state *state = &image->state;
	struct wu_event *event = &window->event;
	struct gl_context *gl = &window->gl;

	switch (code) {
	// Exit
	case 'Q':
		event->exit = true;
		break;

	// Fullscreen
	case 'F':
		window_fullscreen(window);
		return false;
	// Alpha display
	case 'A':
		gl_alpha_toggle(gl, shift ? -1 : 1);
		break;
	// Metadata
	case 'M':
		image_file_print(file, stdout, 2 + shift, file->nr == 1);
		return false;

	// Delete
	case 'D':
		if (!shift && event->rm == rm_no) {
			event->rm = rm_ask;
			term_line_temp("Delete file?"
				" (D to confirm, u to dismiss)");
		} else if (shift && event->rm == rm_ask) {
			event->rm = rm_yes;
		}
		return false;
	// Abort delete
	case 'U':
		if (event->rm == rm_ask) {
			event->rm = rm_no;
		}
		term_line_clear();
		return false;

	// Cycling
	case 'N': // Next
		event->cycle += shift ? 10 : 1;
		break;
	case 'P': // Prev
		event->cycle -= shift ? 10 : 1;
		break;
	// Sub-cycling
	case '<': // Prev
		sub_cycle(window, -1);
		break;
	case '>': // Next
		sub_cycle(window, 1);
		break;
	// Frame cycling
	case ',': // Prev
		frame_cycle(window, -1);
		break;
	case '.': // Next
		frame_cycle(window, 1);
		break;
	case ';':
		frame_cycle(window, -5);
		break;
	case ':':
		frame_cycle(window, 5);
		break;
	case ' ':
		window->win.playing ^= wudec_cur_is_anim(image);
		timespec_present_time(&window->win.timer.play);
		return false;

	// Image movement
	case 'H': // Left
		event->image = ev_transform;
		state->x_offset += dt / state->zoom;
		break;
	case 'J': // Down
		event->image = ev_transform;
		state->y_offset -= dt / state->zoom;
		break;
	case 'K': // Up
		event->image = ev_transform;
		state->y_offset += dt / state->zoom;
		break;
	case 'L': // Right
		event->image = ev_transform;
		state->x_offset -= dt / state->zoom;
		break;

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
		event->image = wudec_zoom(image, state->zoom * exp2f(1.0f/12.0f));
		break;
	case '/':
		event->image = wudec_zoom(image, state->zoom * exp2f(1.0f/-12.0f));
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
		return false;
	case '1': case '2': case '3': case '4':
	case '5': case '6': case '7': case '8': case '9':
		event->image = wudec_zoom(image, (float)(code - '0'));
		return false;
	}
	return true;
}

double event_exec(struct window_context *window) {
	struct window_keymap *held_keys = &window->win.held_keys;
	const bool shift = held_keys->shift;

	struct timespec now;
	const double elapsed = timespec_elapsed(&now, &window->win.timer.key);
	float msecs = (float)elapsed * 1000.0f;
	if (msecs >= 1.0f) {
		window->win.timer.key = now;
	} else {
		msecs = 0.0f;
	}
	const int inc = (int)lroundf(msecs);
	const float mul = shift ? WINDOW_FAST_MOVE_FACTOR : 1.0f;
	msecs *= mul;

	unsigned char *map = window_keymap_map(held_keys);
	held_keys->active = false;
	for (int key = WINDOW_KEYSTART; key < WINDOW_KEYEND; ++key) {
		const unsigned char time = map[key];
		float dt = 16.0f * mul;
		switch (time) {
		case 0:
			continue;
		case key_once:
			map[key] = 0;
			break;
		case 0xff:
			dt = msecs;
			break;
		default:
			map[key] = (unsigned char)iclamp(time + inc,
				key_pressed + 1, 0xff);
			if (time != key_pressed) {
				held_keys->active = true;
				continue;
			}
		}

		if (!apply_event(window, key, dt, shift)) {
			map[key] = 0;
		}
		held_keys->active |= (bool)map[key];
	}
	return elapsed;
}

void event_print_keys(FILE *out) {
	fputs("Keybinds:\n"
		"\tq | Alt+F4 | Ctrl+w | Ctrl+c\n"
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
		"\t\ttwice as fast.\n"

		"\tz | x\n"
		"\t\tRotate counter- or clockwise.\n"

		"\ti | o\n"
		"\t\tMirror horizontally or vertically.\n"

		"\t+ | - | PageUp | PageDown\n"
		"\t\tZoom in or out. The image size is doubled or halved every\n"
		"\t\tthree presses.\n"

		"\t* | / | PageUp+Shift | PageDown+Shift\n"
		"\t\tLike + and -, but size is doubled or halved every twelve\n"
		"\t\tpresses.\n"

		"\t0 | Home\n"
		"\t\tCenter image, and fit to window or scale to 1x, whichever\n"
		"\t\tis smaller.\n"

		"\t= | End\n"
		"\t\tCenter and fit to window.\n"

		"\t1 | Home+Shift\n"
		"\t\t1x zoom.\n"

		"\t2 .. 9\n"
		"\t\t[n]x zoom.\n"

		"\tPointer drag | Pointer scroll\n"
		"\t\tMove viewport.\n"

		"\tVertical scroll + Shift\n"
		"\t\tMove viewport horizontally.\n"

		"\tVertical scroll + Ctrl\n"
		"\t\tZoom centered on pointer.\n", out);
}

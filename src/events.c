#include <stdio.h>
#include <ctype.h>
#include <math.h>

#include "wudefs.h"
#include "common.h"
#include "events.h"
#include "term.h"

#define KEYSTART ' '
#define KEYEND ('Z' + 1)

struct keymap {
	bool shift;
	unsigned char map[KEYEND - KEYSTART];
};

static struct keymap held_keys = {0};

static bool apply_event(struct image_context *image,
struct wu_event *event, const size_t code, const float msecs) {
	const float MAX_ZOOM = 64.0f;
	const float MIN_ZOOM = 1.0f / (MAX_ZOOM * 2);

	const struct image_file *file = &image->file;
	struct wu_state *state = &image->state;

	const bool shift = held_keys.shift;
	float new_zoom = 0;
	int subcycle = 0;
	switch (code) {
	// Exit
	case 'Q':
		event->program = close_window;
		break;
	// Reload file
	case 'R':
		event->program = reload_file;
		break;

	// Fullscreen
	case 'F':
		event->window = toggle_fullscreen;
		break;
	// Alpha display
	case 'A':
		state->alpha = (unsigned char)((state->alpha + 1) % 3);
		event->window = toggle_alpha;
		break;
	// Metadata
	case 'M':
		print_image_information(file, 2 + shift);
		return true;

	// Delete
	case 'D':
		if (!shift && event->rm == no_rm) {
			event->rm = warn_rm;
			term_temp_line("Delete file? (D to confirm, "
				"u to dismiss)");
		} else if (shift && event->rm == warn_rm) {
			event->rm = yes_rm;
		}
		break;
	// Abort delete
	case 'U':
		if (event->rm == warn_rm) {
			event->rm = no_rm;
			term_clear_line();
		}
		break;

	// Cycling
	case 'N': event->file.cycle += shift ? 10 : 1; break;
	case 'P': event->file.cycle -= shift ? 10 : 1; break;
	// Sub-cycling
	case '.':
		subcycle = 1;
		break;
	case ',':
		subcycle = -1;
		break;
	case ':':
		subcycle = 5;
		break;
	case ';':
		subcycle = -5;
		break;
	case ' ':
		state->anim ^= 1;
		break;

	// Image movement
	case 'H':
		event->image = ev_move;
		state->x_offset += msecs / state->zoom;
		break;
	case 'J':
		event->image = ev_move;
		state->y_offset += msecs / state->zoom;
		break;
	case 'K':
		event->image = ev_move;
		state->y_offset -= msecs / state->zoom;
		break;
	case 'L':
		event->image = ev_move;
		state->x_offset -= msecs / state->zoom;
		break;

	// Rotation.
	case 'Z':
		event->image = ev_mirrot;
		state->rotate = (state->rotate + 1) & 3;
		break;
	case 'X':
		event->image = ev_mirrot;
		state->rotate = (state->rotate - 1) & 3;
		break;

	// Mirror
	case 'I':
		event->image = ev_mirrot;
		state->mirror = !state->mirror;
		state->rotate = (state->rotate + 2) & 3;
		break;
	case 'O':
		event->image = ev_mirrot;
		state->mirror = !state->mirror;
		break;

	// Zoom
	case '+':
		new_zoom = fclampf(state->zoom * cbrtf(2.0f),
			MIN_ZOOM, MAX_ZOOM);
		break;
	case '-':
		new_zoom = fclampf(state->zoom * cbrtf(0.5f),
			MIN_ZOOM, MAX_ZOOM);
		break;
	case '0':
		state->x_offset = 0;
		state->y_offset = 0;
		new_zoom = state->fit_zoom;
		break;
	case '1': case '2': case '3': case '4':
	case '5': case '6': case '7': case '8': case '9':
		; const struct raw_img *img = file->sub_img + state->idx;
		new_zoom = (float)(code - '0') * (1/img->dec_scale);
		break;
	}

	if (new_zoom && new_zoom != state->zoom) {
		if (new_zoom > state->zoom) {
			event->image = ev_upscale;
		} else {
			event->image = ev_downscale;
		}
		state->zoom = new_zoom;
	} else if (subcycle) {
		state->sub.cycle += subcycle;
		event->image |= ev_subcycle;
	}
	return false;
}

void event_exec(struct image_context *image, struct wu_event *event,
double secs) {
	float msecs = (float)(secs * 1000);
	const int inc = (int)msecs;
	if (held_keys.shift) {
		msecs *= 2;
	}

	unsigned char *map = held_keys.map - KEYSTART;
	for (size_t key = KEYSTART; key < KEYEND; ++key) {
		const unsigned char time = map[key];
		switch (time) {
		case 0:
			continue;
		case key_external:
			map[key] = 0;
			break;
		case 0xff:
			break;
		default:
			map[key] = (unsigned char)imin(0xff, time + inc);
			if (time != key_press) {
				continue;
			}
		}
		if (apply_event(image, event, key, msecs)) {
			map[key] = 0;
		}
	}
}

void event_add(const enum key_action action, int code, const bool shift) {
	held_keys.shift = shift;
	code = toupper(code);
	if (code >= KEYSTART && code < KEYEND) {
		unsigned char *map = held_keys.map - KEYSTART;
		if (!map[code] || action == key_release) {
			map[code] = action;
		}
	}
}

void print_keys(void) {
	fputs("Keybinds (case insensitive unless specified):\n"

		"\tq | Alt + F4 | Ctrl + w\n"
		"\t\tQuit.\n"

		"\tf | F11\n"
		"\t\tToggle fullscreen.\n"

		"\ta\n"
		"\t\tCycle between alpha as transparency, as checkerboard\n"
		"\t\tpattern, or disabled (with transparent colors visible).\n"

		"\tm | M\n"
		"\t\tPrint unabreviatted metadata. For 'm', omit fields that\n"
		"\t\twould occupy more than a line or two of text. For 'M',\n"
		"\t\tomit nothing.\n"

		"\tr\n"
		"\t\tReload current file.\n"

		"\td\n"
		"\t\tPrompt to delete the current file. 'D' to confirm, 'u'\n"
		"\t\tto dismiss.\n"

		"\tn | p | N | P\n"
		"\t\tGo to next or previous image. If uppercase, skip 10\n"
		"\t\timages at a time.\n"

		"\t. | , | : | ;\n"
		"\t\tFor period and comma, go to next or previous sub-image.\n"
		"\t\tFor colons, skip 5 sub-images at a time.\n"

		"\th | j | k | l | H | J | K | L | Arrow keys\n"
		"\t\tMove viewport to the left, down, up, and right,\n"
		"\t\trespectively. If uppercase, move twice as much.\n"

		"\tz | x\n"
		"\t\tRotate counter- or clockwise.\n"

		"\ti | o\n"
		"\t\tMirror horizontally or vertically.\n"

		"\t+ | - | PageUp | PageDown\n"
		"\t\tZoom in or out.\n"

		"\t0 | End\n"
		"\t\tFit to window and center.\n"

		"\t1 | Home\n"
		"\t\t1x zoom.\n"

		"\t2 .. 9\n"
		"\t\t[n]x zoom.\n",
		stderr);
}

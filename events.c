#include <stdio.h>
#include <ctype.h>
#include <math.h>

#include "wudefs.h"
#include "common.h"
#include "events.h"

#define KEYSTART ' '
#define KEYEND 'Z'
struct keymap {
	unsigned char map[KEYEND - KEYSTART + 1];
	bool shift;
};

static struct keymap held_keys = {0};

static void apply_event(const struct image_file *file, struct wu_state *state,
struct wu_event *event, const unsigned char code, const float msecs) {
	const float MAX_ZOOM = 64.0f;
	const float MIN_ZOOM = 1.0f / (MAX_ZOOM * 2);

	const bool shift = held_keys.shift;
	float new_zoom = 0;
	switch (code) {
	// Exit
	case 'Q':
		event->program = close_window;
		break;
	// Fullscreen
	case 'F':
		event->window = toggle_fullscreen;
		break;
	// Alpha display
	case 'A':
		event->window = toggle_alpha;
		state->alpha = (unsigned char)((state->alpha + 1) % 3);
		break;
	// Metadata
	case 'M':
		print_image_information(file);
		break;
	// Refresh
	case 'R':
		event->program = reload_file;
		break;

	// Delete
	case 'D':
		if (!shift && event->rm == no_rm) {
			event->rm = warn_rm;
			print_temp_line("Delete file? (D to confirm, "
				"u to dismiss.)");
		} else if (shift && event->rm == warn_rm) {
			event->rm = yes_rm;
		}
		break;
	// Abort delete
	case 'U':
		if (event->rm == warn_rm) {
			event->rm = no_rm;
			print_temp_line("\r" CLEAR_LINE);
		}
		break;

	// Cycling
	case 'N': event->file.cycle += shift ? 10 : 1; break;
	case 'P': event->file.cycle -= shift ? 10 : 1; break;
	// Sub-cycling
	case '.':
		state->sub.cycle += 1;
		state->anim |= 1;
		break;
	case ',':
		state->sub.cycle -= 1;
		state->anim |= 1;
		break;
	case ':':
		state->sub.cycle += 5;
		state->anim |= 1;
		break;
	case ';':
		state->sub.cycle -= 5;
		state->anim |= 1;
		break;
	case ' ':
		state->anim ^= 1;
		break;

	// Image movement
	case 'H':
		event->image |= move;
		state->x_offset += msecs / state->zoom;
		break;
	case 'J':
		event->image |= move;
		state->y_offset += msecs / state->zoom;
		break;
	case 'K':
		event->image |= move;
		state->y_offset -= msecs / state->zoom;
		break;
	case 'L':
		event->image |= move;
		state->x_offset -= msecs / state->zoom;
		break;

	// Rotation.
	case 'Z':
		event->image |= mirrot;
		state->rotate = (state->rotate + 1) & 3;
		break;
	case 'X':
		event->image |= mirrot;
		state->rotate = (state->rotate - 1) & 3;
		break;

	// Mirror
	case 'I':
		event->image |= mirrot;
		state->mirror = !state->mirror;
		state->rotate = (state->rotate + 2) & 3;
		break;
	case 'O':
		event->image |= mirrot;
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
		new_zoom = (float)(code - '0') * (1/state->dec_scale);
		break;
	}

	if (new_zoom && new_zoom != state->zoom) {
		if (new_zoom > state->zoom) {
			event->image |= up_scale;
		} else {
			event->image |= down_scale;
		}
		state->zoom = new_zoom;
	}
}

void exec_events(const struct image_file *file, struct wu_state *state,
struct wu_event *event, float msecs) {
	const int inc = (int)msecs;
	if (held_keys.shift) {
		msecs *= 2;
	}
	const size_t map_size = sizeof(held_keys.map);
	unsigned char *map = held_keys.map - KEYSTART;
	for (unsigned char key = KEYSTART; key < map_size + KEYSTART; ++key) {
		switch (map[key]) {
		case 0x00: continue;
		case 0xff:
			apply_event(file, state, event, key, msecs);
			break;

		case key_external:
			apply_event(file, state, event, key, msecs);
			map[key] = 0;
			break;
		case key_press:
			apply_event(file, state, event, key, msecs);
			// Fallthrough
		default:
			map[key] = (unsigned char)imin(0xff, map[key] + inc);
		}
	}
}

void add_event(const enum key_action action, const unsigned char code,
const bool shift) {
	held_keys.shift = shift;
	if (code >= KEYSTART && code < KEYSTART + sizeof(held_keys.map)) {
		unsigned char *map = held_keys.map - KEYSTART;
		if (!map[code]) {
			map[code] = action;
		} else if (action == key_release) {
			map[code] = 0;
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

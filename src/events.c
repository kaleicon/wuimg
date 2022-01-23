#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

#include "wudefs.h"
#include "common.h"
#include "events.h"
#include "term.h"

void event_lift(struct wu_keymap *held_keys) {
	memset(held_keys, 0, sizeof(*held_keys));
}

static unsigned char * get_map(struct wu_keymap *held_keys) {
	return held_keys->map - WU_KEYSTART;
}

static bool apply_event(struct image_context *image,
struct wu_event *event, const int code, const float msecs, const bool shift) {
	const float MAX_ZOOM = 64.0f;
	const float MIN_ZOOM = 1.0f / MAX_ZOOM;

	const struct image_file *file = &image->file;
	struct wu_state *state = &image->state;

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
		event->window = toggle_alpha;
		break;
	// Metadata
	case 'M':
		image_file_print(file, 1 + shift);
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
		state->anim_playing = !state->anim_playing;
		break;

	// Image movement
	case 'H': // Left
		event->image = ev_move;
		state->x_offset += msecs / state->zoom;
		break;
	case 'J': // Down
		event->image = ev_move;
		state->y_offset -= msecs / state->zoom;
		break;
	case 'K': // Up
		event->image = ev_move;
		state->y_offset += msecs / state->zoom;
		break;
	case 'L': // Right
		event->image = ev_move;
		state->x_offset -= msecs / state->zoom;
		break;

	// Rotation.
	case 'Z': // Counterclockwise
		event->image = ev_mirrot;
		state->rotate = (state->rotate + 1) & 3;
		break;
	case 'X': // Clockwise
		event->image = ev_mirrot;
		state->rotate = (state->rotate - 1) & 3;
		break;

	// Mirror
	case 'I': // Horizontal
		event->image = ev_mirrot;
		state->mirror = !state->mirror;
		state->rotate = (state->rotate + 2) & 3;
		break;
	case 'O': // Vertical
		event->image = ev_mirrot;
		state->mirror = !state->mirror;
		break;

	// Zoom
	case '+':
		event->image = image_zoom(image,
			fclampf(state->zoom * cbrtf(2.0f), MIN_ZOOM, MAX_ZOOM));
		break;
	case '-':
		event->image = image_zoom(image,
			fclampf(state->zoom * cbrtf(0.5f), MIN_ZOOM, MAX_ZOOM));
		break;
	case '0':
		state->x_offset = 0;
		state->y_offset = 0;
		event->image = image_zoom(image, state->fit_zoom);
		break;
	case '1': case '2': case '3': case '4':
	case '5': case '6': case '7': case '8': case '9':
		; const struct raw_img *img = file->sub_img + state->idx;
		event->image = image_zoom(image,
			(float)(code - '0') * (1/img->dec_scale));
		break;
	}
	return false;
}

double event_exec(struct wu_keymap *held_keys, struct image_context *image,
struct wu_event *event, double secs) {
	float msecs = (float)(secs * 1000);
	const int inc = (int)msecs;
	if (held_keys->shift) {
		msecs *= 2;
	}

	unsigned char *map = get_map(held_keys);
	for (int key = WU_KEYSTART; key < WU_KEYEND; ++key) {
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
		if (apply_event(image, event, key, msecs, held_keys->shift)) {
			map[key] = 0;
		}
	}
	return secs;
}

void event_add(struct wu_keymap *held_keys, const enum key_action action,
int code, const bool shift) {
	held_keys->shift = shift;
	code = toupper(code);
	if (code >= WU_KEYSTART && code < WU_KEYEND) {
		unsigned char *map = get_map(held_keys);
		if (!map[code] || action == key_release) {
			map[code] = action;
		}
	}
}

void print_keys(void) {
	fputs("Keybinds (case insensitive unless specified):\n"

		"\tq | Alt+F4 | Ctrl+w\n"
		"\t\tQuit.\n"

		"\tf | F11\n"
		"\t\tToggle fullscreen.\n"

		"\ta\n"
		"\t\tCycle between alpha blending enabled, as checkerboard\n"
		"\t\tpattern, or opaque.\n"

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
		"\t\tGo to the previous or next sub-image, respectively.\n"

		"\t. | , | : | ;\n"
		"\t\tFor period and comma, go to the next or previous frame\n"
		"\t\twithin an animated sub-image. For colons, skip 5 frames\n"
		"\t\tat a time.\n"

		"\th | j | k | l | H | J | K | L | Arrow keys\n"
		"\t\tMove viewport to the left, down, up, and right,\n"
		"\t\trespectively. If shift is pressed (uppercase), move\n"
		"\t\ttwice as much.\n"

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
		stdout);
}

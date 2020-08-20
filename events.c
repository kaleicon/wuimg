#include <stdio.h>
#include <ctype.h>
#include <math.h>

#include "wudefs.h"
#include "common.h"
#include "events.h"

#define KEYSTART ' '
#define KEYEND 'z'
static unsigned char held_keys[KEYEND - KEYSTART + 1] = {0};

static void apply_event(struct wu_state *state, struct wu_event *event,
struct wu_pos *filepos, const unsigned code, float msecs) {
	const float MAX_ZOOM = 64.0f;
	const float MIN_ZOOM = 1.0f / (MAX_ZOOM * 2);

	msecs *= (float)(1 + (bool)isupper((int)code)); // Casts everywhere
	float new_zoom = 0;
	switch (code) {
	// Exit
	case 'q':
		event->program = close_window;
		break;
	// Fullscreen
	case 'f':
		event->window = toggle_fullscreen;
		break;
	// Alpha display
	case 'a':
		event->window = toggle_alpha;
		state->alpha_checkers = !state->alpha_checkers;
		break;

	// Cycling
	case 'n': filepos->cycle += 1; break;
	case 'N': filepos->cycle += 10; break;
	case 'p': filepos->cycle -= 1; break;
	case 'P': filepos->cycle -= 10; break;
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

	// Refresh
	case 'r':
		event->program = reload_file;
		break;

	// Delete
	case 'd':
		if (event->rm == no_rm) {
			event->rm = warn_rm;
			print_temp_line("Delete file? (D to confirm, u to dismiss.)");
		}
		break;
	case 'D':
		if (event->rm == warn_rm) {
			event->rm = yes_rm;
		}
		break;
	// Abort delete
	case 'u':
		if (event->rm) {
			event->rm = no_rm;
			print_temp_line("Not deleted.");
		}
		break;

	// Image movement
	case 'h': case 'H':
		event->image |= move;
		state->x_offset += msecs / state->zoom;
		break;
	case 'j': case 'J':
		event->image |= move;
		state->y_offset += msecs / state->zoom;
		break;
	case 'k': case 'K':
		event->image |= move;
		state->y_offset -= msecs / state->zoom;
		break;
	case 'l': case 'L':
		event->image |= move;
		state->x_offset -= msecs / state->zoom;
		break;

	// Rotation.
	case 'z':
		event->image |= mirrot;
		state->rotate = (state->rotate + 1) & 3;
		break;
	case 'x':
		event->image |= mirrot;
		state->rotate = (state->rotate - 1) & 3;
		break;

	// Mirror
	case 'i':
		event->image |= mirrot;
		state->mirror = !state->mirror;
		state->rotate = (state->rotate + 2) & 3;
		break;
	case 'o':
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
		new_zoom = (float)(code - '0');
		break;
	}

	if (new_zoom && new_zoom != state->zoom) {
		const enum image_event ev = (new_zoom > state->zoom)
			? up_scale : down_scale;
		// Fix conversion warning
		event->image = (enum image_event)(event->image | ev);
		state->zoom = new_zoom;
	}
}

void exec_events(struct wu_state *state, struct wu_event *event,
struct wu_pos *filepos, const float msecs) {
	const int inc = (int)msecs;
	for (unsigned i = 0; i < sizeof(held_keys); ++i) {
		const unsigned key = i + KEYSTART;
		switch (held_keys[i]) {
		case 0x00: continue;

		case key_external:
			apply_event(state, event, filepos, key, msecs);
			held_keys[i] = 0;
			break;
		case 0xff:
			apply_event(state, event, filepos, key, msecs);
			break;

		case key_press:
			apply_event(state, event, filepos, key, msecs);
			// Fallthrough
		default:
			held_keys[i] = (unsigned char)
				imin(0xff, held_keys[i] + inc);
		}
	}
}

void add_event(const enum key_action action, unsigned code) {
	if (code >= KEYSTART && code < sizeof(held_keys) + KEYSTART) {
		code -= KEYSTART;
		if (!held_keys[code]) {
			held_keys[code] = (unsigned char)action;
		} else if (action == key_release) {
			held_keys[code] = 0;
		}
	}
}

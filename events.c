#include <stdio.h>
#include <math.h>

#include "wudefs.h"
#include "common.h"
#include "events.h"

#define KEYSTART ' '
#define KEYEND 'z'
static unsigned char held_keys[KEYEND - KEYSTART + 1] = {0};

static void apply_event(struct wu_state *state, struct wu_event *event,
struct wu_pos *filepos, const unsigned code, const float msecs) {
	const float MAX_ZOOM = 64.0f;
	const float MIN_ZOOM = 1.0f / (MAX_ZOOM * 2);

	switch (code) {
	// Exit
	case 'q':
		event->program = close_window;
		break;
	// Fullscreen
	case 'f':
		event->window = toggle_fullscreen;
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
	case 'h':
		event->image |= move;
		state->x_offset += 1 / state->zoom * msecs;
		break;
	case 'j':
		event->image |= move;
		state->y_offset += 1 / state->zoom * msecs;
		break;
	case 'k':
		event->image |= move;
		state->y_offset -= 1 / state->zoom * msecs;
		break;
	case 'l':
		event->image |= move;
		state->x_offset -= 1 / state->zoom * msecs;
		break;

	// Rotation. Negative is clockwise
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
		event->image |= scale;
		state->zoom = fclampf(state->zoom * cbrtf(2),
			MIN_ZOOM, MAX_ZOOM);
		break;
	case '-':
		event->image |= scale;
		state->zoom = fclampf(state->zoom * cbrtf(0.5f),
			MIN_ZOOM, MAX_ZOOM);
		break;
	case '0':
		event->image |= scale;
		state->x_offset = 0;
		state->y_offset = 0;
		state->zoom = state->fit_zoom;
		break;
	case '1': case '2': case '3': case '4':
	case '5': case '6': case '7': case '8': case '9':
		event->image |= scale;
		state->zoom = (float)(code - '0');
		break;
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

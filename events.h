#ifndef WU_EVENTS
#define WU_EVENTS

#include <stdbool.h>

#include "wudefs.h"

enum window_event {
	toggle_fullscreen = 1,
	toggle_alpha,
};

enum program_event {
	close_window = 1,
	reload_file,
};

enum remove_event {
	disable_rm = -1,
	no_rm = 0,
	warn_rm,
	yes_rm,
};

struct wu_event {
	enum image_event image:8;
	enum window_event window:8;
	enum program_event program:8;
	enum remove_event rm:8;
};

enum key_action {
	key_release = 0,
	key_external = 1,
	key_press = 2,
};

/*
struct wu_pos {
	int cycle;
	float acc;
};

struct wu_state {
	struct wu_pos sub;

	enum anim_state {
		playing = 2,
		paused = 3,
	} anim:8;

	unsigned char rotate;
	bool mirror;

	float x_offset;
	float y_offset;
	float fit_zoom;
	float zoom;
};
*/

void exec_events(struct wu_state *state, struct wu_event *event,
struct wu_pos *filepos, float msecs);

void add_event(enum key_action action, unsigned code);

#endif /* WU_EVENTS */

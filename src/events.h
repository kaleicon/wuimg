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
	no_rm = 0,
	warn_rm,
	yes_rm,
//	disable_rm,
};

struct wu_event {
	struct wu_cycle file;
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

void event_exec(struct image_context *image, struct wu_event *event,
double secs);

void event_add(enum key_action action, int code, bool shift);

void print_keys(void);

#endif /* WU_EVENTS */

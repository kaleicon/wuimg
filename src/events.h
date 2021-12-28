#ifndef WU_EVENTS
#define WU_EVENTS

#include <stdbool.h>

#include "wudefs.h"

#define WU_KEYSTART ' '
#define WU_KEYEND ('Z' + 1)

struct wu_keymap {
        bool shift;
        unsigned char map[WU_KEYEND - WU_KEYSTART];
};

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
};

struct wu_event {
	int cycle;
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

void event_lift(struct wu_keymap *held_keys);

double event_exec(struct wu_keymap *held_keys, struct image_context *image,
struct wu_event *event, double secs);

void event_add(struct wu_keymap *held_keys, enum key_action action, int code,
bool shift);

void print_keys(void);

#endif /* WU_EVENTS */

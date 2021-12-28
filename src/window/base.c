#include <math.h>

#include "base.h"

static double monoclock_diff(const struct timespec start,
const struct timespec end) {
	const double nanos_per_sec = 1000000000;
	return (double)(end.tv_sec - start.tv_sec)
		+ (double)(end.tv_nsec - start.tv_nsec) / nanos_per_sec;
}

double window_exec_events(struct window_public *pub) {
	const struct timespec start = pub->timer;
	clock_gettime(CLOCK_MONOTONIC, &pub->timer);
	return event_exec(&pub->held_keys, &pub->image, &pub->event,
		monoclock_diff(start, pub->timer));
}

enum trit window_size_update(struct window_public *pub, const unsigned w,
const unsigned h) {
	if (w && h) {
		struct display_dims *fb = &pub->image.conf.fb;
		if (fb->w != w || fb->h != h) {
			fb->w = w;
			fb->h = h;
			gl_viewport(&pub->gl, fb);
			return trit_true;
		}
		return trit_false;
	}
	return trit_what;
}

void window_scroll_axis(struct window_cursor_axis *axis, const double offset) {
	if (fpclassify(offset) == FP_NORMAL) {
		axis->scroll = (float)(axis->scroll + offset);
	}
}

void window_scroll(struct window_cursor *cursor, const double x, const double y) {
	window_scroll_axis(&cursor->x, x);
	window_scroll_axis(&cursor->y, y);
}

void window_cursor_apply_diff(struct window_cursor *cur,
struct window_public *pub, const double x, const double y) {
	struct wu_state *state = &pub->image.state;
	if (cur->pressed) {
		const double zoom = fmax(1 / state->zoom, 1);
		state->x_offset += (float)((x - cur->x.pos) * zoom);
		state->y_offset += (float)((y - cur->y.pos) * zoom);
		pub->gl.update_matrix = true;
	}
	cur->x.pos = (float)x;
	cur->y.pos = (float)y;
}

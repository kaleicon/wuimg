#ifndef WUDEFS
#define WUDEFS

#include <stdio.h>
#include <stddef.h>
#include <stdbool.h>

#include "misc/wustr.h"
#include "raster/wuimg.h"

struct wu_state {
	int idx;
	int frame;
	bool anim_playing;

	unsigned char rotate;
	bool mirror;

	float zoom;
	float x_offset;
	float y_offset;
};

enum image_event {
	ev_end = 0,
	ev_subcycle = 1,
	ev_frame = 1 << 1,
	ev_upscale = 1 << 2,
	ev_downscale = 1 << 3,
	ev_scale = ev_upscale | ev_downscale,
	ev_move = 1 << 4,
	ev_mirrot = 1 << 5,
};

struct image_file {
	FILE *ifp;
	size_t nr;
	struct wuimg *sub_img;
	struct wu_tree metadata;

	struct pix_rgba8 bg;

	enum image_event events;
	void *restrict dec_state; // Used by decoder for callbacks

	struct wustr errors;
};

struct image_context {
	const char *name;
	struct image_file file;
	struct wu_state state;
	struct wu_conf conf;
	int fmt_id;
};


struct wuimg * realloc_sub_images(struct image_file *file, size_t nr);

struct wuimg * alloc_sub_images(struct image_file *file, size_t nr);

void image_file_free_if_single(struct image_file *file);

void image_file_print(const struct image_file *file, int verbosity);

void image_file_normalize(struct image_file *file);

enum wu_error image_file_total_decoded(struct image_file *file, size_t o);

void image_file_error_append(struct image_file *file, const char *str);

void image_file_status_append(struct image_file *file, enum wu_error status);

void image_file_free(struct image_file *file);


struct wuimg * image_cur_sub_img(const struct image_context *image);

enum image_event image_zoom(struct image_context *image, float new_zoom);

enum image_event image_sub_cycle(struct image_context *image, int steps);

enum image_event image_frame_cycle(struct image_context *image, int steps);

void image_reset(struct image_context *image);

#endif /* WUDEFS */

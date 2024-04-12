// SPDX-License-Identifier: 0BSD
#ifndef WUDEFS
#define WUDEFS

#include <stddef.h>
#include <stdbool.h>

#include "misc/file.h"
#include "misc/wustr.h"
#include "raster/wuimg.h"

#define WU_SCALING_POW 6

struct wu_state {
	int idx;
	int frame;
	bool anim_playing;

	unsigned char rotate;
	bool mirror;

	float zoom;
	float x_offset;
	float y_offset;

	double time;
};

enum image_event {
	ev_none = 0,
	ev_subcycle = 1,
	ev_frame = 1 << 1,
	ev_upscale = 1 << 2,
	ev_downscale = 1 << 3,
	ev_scale = ev_upscale | ev_downscale,
	ev_move = 1 << 4,
	ev_mirrot = 1 << 5,
	ev_time = 1 << 6,
};

struct image_file {
	FILE *ifp;
	struct map_info map;

	size_t nr;
	struct wuimg *sub_img;
	struct wu_tree metadata;

	struct pix_rgba8 bg;

	bool keep_file;
	bool keep_map;
	void *restrict dec_state; // Used by decoder for callbacks

	struct wustr errors;
};

typedef enum wu_error (*fmt_dec_t)(struct image_file *infile,
	const struct wu_conf *wuconf);
typedef enum wu_error (*fmt_callback_t)(struct image_file *infile,
	const struct wu_conf *wuconf, struct wu_state *state, enum image_event ev);
typedef void (*fmt_end_t)(struct image_file *infile);

struct image_fn {
	bool mmap;
	fmt_dec_t dec;
	fmt_callback_t callback;
	fmt_end_t end;
};

struct image_context {
	const char *name;
	struct image_file file;
	struct wu_state state;
	struct wu_conf conf;
	const struct image_fn *fn;
};

void image_file_free_end(struct image_file *infile);


struct wuimg * realloc_sub_images(struct image_file *file, size_t nr);

struct wuimg * alloc_sub_images(struct image_file *file, size_t nr);

void image_file_free_if_single(struct image_file *file);

void image_file_print(const struct image_file *file, int verbosity,
bool unloaded_too);

void image_file_normalize(struct image_file *file);

enum wu_error image_file_total_decoded(struct image_file *file, size_t o);

void image_file_strerror_append(struct image_file *file, const char *str);

void image_file_error_append(struct image_file *file, enum wu_error status);

void image_file_free(struct image_file *file);


enum image_event image_cur_events(const struct image_context *image);

struct wuimg * image_cur_sub_img(const struct image_context *image);

bool image_cur_is_anim(const struct image_context *image);

enum image_event image_zoom(struct image_context *image, float new_zoom);

enum image_event image_sub_cycle(struct image_context *image, int steps);

enum image_event image_frame_cycle(struct image_context *image, int steps);

void image_reset(struct image_context *image);

#endif /* WUDEFS */

// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2019 kaleido
#ifndef WUDEFS
#define WUDEFS

#include "raster/wuimg.h"

#define WU_SCALING_POW 6

struct wu_state {
	int idx; // current sub-image
	int frame; // current frame

	unsigned char rotate;
	bool mirror;

	float zoom;
	float x_offset;
	float y_offset;

	double time;

	struct display_dims fb;
};

enum image_event {
	ev_none = 0,

	// Request current image dimensions and metadata to be set
	ev_metadata = 1 << 0,

	// Decode current image. Only if wuimg.data = NULL
	ev_subcycle = 1 << 1,

	// Render current frame. Only if wuimg.anim != NULL
	ev_frame = 1 << 2,

	// Redraw according to time displayed. Only if wuimg.evolving = true
	ev_time = 1 << 3,

	// Redraw with current transform. Only if wuimg.scalable = true
	ev_transform = 1 << 4,
};

struct image_file {
	FILE *ifp;
	long off;
	struct wuptr map;

	size_t nr;
	struct wuimg *sub_img;
	struct wutree metadata;

	const struct wu_conf *conf;
	void *restrict dec_state; // Used by decoder for callbacks
	struct wustr errors;

	const char *name;
	uint8_t ext[8]; // Lowercase extension from `name`
	struct pix_rgba8 bg;

	bool keep_file;
	bool keep_map;
	bool stat;
};

typedef struct wu_st (*fmt_init_t)(struct image_file *infile);
typedef struct wu_st (*fmt_event_t)(struct image_file *infile,
	struct wu_state *state, enum image_event ev);

typedef void (*fmt_end_t)(struct image_file *infile);

struct image_fn {
	// Decoder wants the file in memory (in image_file.map)
	bool mmap;

	// Pre-alloc a single image (image_file.nr = 1) before calling .init
	bool alloc_single;

	/* Validate image params and alloc buffer after ev_metadata and
	 * before ev_subcycle callbacks */
	bool alloc_on_subcycle;

	/* Alloc image_file.dec_state with this size before calling .init, free
	 * after calling .end */
	uint16_t state_size;

	/* Decoder initialization:
	 * - If NULL, .alloc_single must be true.
	 * - If image_file.nr is set and image_file.sub_img is NULL, it'll be
	 *   allocated before events are called.
	 * - Sub-images may be decoded in one go if it's more convenient (for
	 *   instance, if it's unknown how many there are). No subcycle events
	 *   will be called then. */
	fmt_init_t init;

	/* Decoder events:
	 * - If NULL, .init must have decoded all sub-images, and they must all
	 *   be static.
	 * - If .alloc_on_subcycle is false, it's up to the decoder to perform
	 *   validation and allocation.
	 * - Should return WU_NO_CHANGE for any unused event. */
	fmt_event_t event;

	// Decoder cleanup. Can be NULL.
	fmt_end_t end;
};

struct wuimg * realloc_sub_images(struct image_file *file, size_t nr);

struct wuimg * alloc_sub_images(struct image_file *file, size_t nr);

void image_file_free_if_single(struct image_file *file);

size_t image_file_print_single(const struct image_file *file, FILE *out,
int verbosity, size_t i, const char *short_end);

void image_file_print(const struct image_file *file, FILE *out, int verbosity,
bool unloaded_too);

size_t image_file_size(const struct image_file *file);

enum wu_error image_file_total_decoded(struct image_file *file, size_t o);

void image_file_error_print(const struct image_file *file, enum wu_error err,
FILE *out);

void image_file_strerror_append(struct image_file *file, const char *str);

void image_file_error_append(struct image_file *file, enum wu_error status);

void image_file_free(struct image_file *file);

#endif /* WUDEFS */

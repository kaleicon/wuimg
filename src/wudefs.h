// SPDX-License-Identifier: 0BSD
#ifndef WUDEFS
#define WUDEFS

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

	struct display_dims fb;
};

enum image_event {
	ev_none = 0,
	ev_subcycle = 1,
	ev_frame = 1 << 1,
	ev_time = 1 << 2,
	ev_transform = 1 << 3,
};

struct image_file {
	FILE *ifp;
	struct wuptr map;

	size_t nr;
	struct wuimg *sub_img;
	struct wutree metadata;

	void *restrict dec_state; // Used by decoder for callbacks
	struct wustr errors;

	uint8_t ext[8];
	struct pix_rgba8 bg;

	bool keep_file;
	bool keep_map;
	bool stat;
};

typedef struct wu_st (*fmt_init_t)(struct image_file *infile,
	const struct wu_conf *conf);
typedef struct wu_st (*fmt_event_t)(struct image_file *infile,
	const struct wu_conf *conf, struct wu_state *state, enum image_event ev);

typedef enum wu_error (*fmt_dec_t)(struct image_file *infile,
	const struct wu_conf *conf);
typedef enum wu_error (*fmt_callback_t)(struct image_file *infile,
	const struct wu_conf *conf, struct wu_state *state, enum image_event ev);

typedef void (*fmt_end_t)(struct image_file *infile);

struct image_fn {
	bool mmap;
	bool alloc_single;
	uint16_t state_size;
	fmt_init_t init;
	fmt_event_t event;
	fmt_dec_t dec; // Old-style init
	fmt_callback_t callback; // Old-style event
	fmt_end_t end;
};

struct fmt_desc {
	char name[8];
	const char *description;
	bool is_auto;
	union {
		const struct image_fn *fn;
		const struct wuptr *desc;
	} dec;
};

struct image_context {
	const char *name;
	struct image_file file;
	struct wu_state state;
	struct wu_conf conf;
	struct fmt_desc desc;
};

struct wuimg * realloc_sub_images(struct image_file *file, size_t nr);

struct wuimg * alloc_sub_images(struct image_file *file, size_t nr);

void image_file_free_if_single(struct image_file *file);

void image_file_print(const struct image_file *file, FILE *out, int verbosity,
bool unloaded_too);

enum wu_error image_file_total_decoded(struct image_file *file, size_t o);

void image_file_error_print(const struct image_file *file, enum wu_error err,
FILE *out);

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

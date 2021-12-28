#ifndef WUDEFS
#define WUDEFS

#include <stdio.h>
#include <stddef.h>
#include <stdbool.h>

#include "conf.h"
#include "wustr.h"
#include "wutree.h"
#include "raster/pix.h"
#include "raster/pal.h"

enum wu_error {
	wu_no_change = -1, // For callbacks
	wu_ok = 0,
	wu_alloc_error,
	wu_unknown_file_type,
	wu_invalid_params,
	wu_open_error,
	wu_unexpected_eof,
	wu_invalid_signature,
	wu_invalid_header,
	wu_exceeds_size_limit,
	wu_unsupported_feature,
	wu_decoding_error,
	wu_unknown_error,
};

struct wu_state {
	int idx;
	int cycle;
	enum anim_state {
		anim_playing = 2,
		anim_paused = 3, // For toggling with '^ 1'
	} anim:8;

	unsigned char rotate;
	bool mirror;

	float x_offset;
	float y_offset;
	float fit_zoom;
	float zoom;
};

enum image_event {
	ev_end = 0,
	ev_subcycle = 1,
	ev_upscale = 1 << 1,
	ev_downscale = 1 << 2,
	ev_scale = ev_upscale | ev_downscale,
	ev_move = 1 << 3,
	ev_mirrot = 1 << 4,
};

struct plane_dim {
	uint8_t subsamp;
};

struct plane_info {
	unsigned char *ptr;
	struct plane_dim x, y;
	size_t w, h;
	size_t stride;
	size_t size;
};

struct image_planes {
	bool yuva;
	bool expand_range;
	uint8_t v_pad;
	struct plane_info p[4];
};

enum image_mode {
	image_mode_raw = 0,
	image_mode_palette = 1,
	image_mode_planar = 2,
};
/*
struct frame_info {
	size_t x, y;
	size_t w, h;
	int msec;
};

struct image_frames {
	size_t nr;
	struct frame_info f[];
};*/

struct raw_img {
	unsigned char *restrict data;
	union {
		struct raster_pal *palette;
		struct image_planes *planes;
	} u;

	size_t w, h;
	unsigned char channels;
	unsigned char bitdepth;
	unsigned char alignment;
	enum pix_layout layout:8;
	enum pix_attr attr:8;
	enum image_mode mode:8;

	unsigned char rotate;
	bool mirror:1; // Vertical mirror. Horizontal is mirror + 2rotate
	bool disable_alpha:1;

//	struct image_frames *frames;
	int msec;
	float dec_scale;

	char *id;
};

struct image_file {
	FILE *ifp;
	size_t nr;
	struct raw_img *sub_img;
	struct wu_tree metadata;

	struct pix_rgba8 bg;
	bool is_animation;

	enum image_event events:8;
	void *restrict dec_state; // Used by decoder for callbacks

	struct wustr errors;
};

struct image_context {
	const char *name;
	struct image_file file;
	int fmt_id;
	struct wu_conf conf;
	struct wu_state state;
};

const char * wu_error_message(enum wu_error err);


int raw_img_geom_hash(const struct raw_img *img);

size_t raw_img_stride(const struct raw_img *img);

size_t raw_img_size(const struct raw_img *img);

size_t raw_img_addbuf(struct raw_img *img);

void raw_img_plane_resolve(struct raw_img *img);

bool raw_img_plane_alloc(struct raw_img *img);

void raw_img_plane_subsamp(struct raw_img *img, const enum pix_subsampling s);

struct image_planes * raw_img_plane_init(struct raw_img *img);

bool raw_img_plane_from_params(struct raw_img *img);

struct raster_pal * raw_img_set_palette(struct raw_img *img,
struct raster_pal *pal);

void raw_img_clear(struct raw_img *img);


struct raw_img * realloc_sub_images(struct image_file *file, size_t nr);

struct raw_img * alloc_sub_images(struct image_file *file, size_t nr);

void image_file_print(const struct image_file *file, int verbosity);

void image_file_normalize(struct image_file *file);

enum wu_error image_file_total_decoded(struct image_file *file, const size_t o);

void image_file_error_append(struct image_file *file, const char *str);

void image_file_free(struct image_file *file);

size_t image_fit_factor(const struct wu_conf *conf, size_t w, size_t h,
size_t max, bool partial_decode);

#endif /* WUDEFS */

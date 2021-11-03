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

#define WU_CANON_NAME "wu"

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

struct wu_cycle {
	int cycle;
	float acc;
};

struct wu_state {
	int idx;
	struct wu_cycle sub;
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

struct yuva_geom {
	size_t w, h;
	size_t stride;
	size_t size;
};

struct yuva_info {
	unsigned char *yuva[4];
	struct yuva_geom ya;
	struct yuva_geom uv;
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

struct raw_img {
	unsigned char *restrict data;
	struct raster_pal *palette;

	size_t w, h;
	unsigned char channels;
	unsigned char bitdepth;
	unsigned char alignment;
	enum pix_layout layout:8;
	enum pix_attr attr:8;

	unsigned char rotate;
	bool mirror:1; // Vertical mirror. Horizontal is mirror + 2rotate
	bool disable_alpha:1;
	bool yuva:1;
	enum pix_subsampling subsamp:8;

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

	struct wustr_mut errors;
};

struct image_context {
	const char *name;
	struct image_file file;
	int fmt_id;
	struct wu_conf conf;
	struct wu_state state;
};

const char * wu_error_message(enum wu_error err);

void raw_img_yuva_info(const struct raw_img *img, struct yuva_info *info);

size_t raw_img_stride(const struct raw_img *img);

size_t raw_img_size(const struct raw_img *img);

size_t raw_img_addbuf(struct raw_img *img);

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

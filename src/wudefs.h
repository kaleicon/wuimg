#ifndef WUDEFS
#define WUDEFS

#include <stdio.h>
#include <stddef.h>
#include <stdbool.h>

#include "conf.h"
#include "wustr.h"
#include "wutree.h"
#include "raster/alpha.h"
#include "raster/color.h"
#include "raster/compost.h"
#include "raster/pix.h"
#include "raster/pal.h"

#define IMG_DATA_BORROWED ((void *)-1)

enum wu_error {
	wu_no_change = -1, // For callbacks
	wu_ok = 0,
	wu_alloc_error,
	wu_open_error,
	wu_unknown_file_type,
	wu_unexpected_eof,
	wu_invalid_signature,
	wu_invalid_header,
	wu_invalid_params,
	wu_unsupported_feature,
	wu_no_image_data,
	wu_exceeds_size_limit,
	wu_int_overflow,
	wu_decoding_error,
	wu_display_error,
	wu_unknown_error,
};

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
	align_t v_pad;
	struct plane_info p[];
};

enum image_mode {
	image_mode_raw = 0,
	image_mode_palette = 1,
	image_mode_planar = 2,
};

struct image_frames {
	size_t nr;
	struct frame_info f[];
};

struct raw_img {
	unsigned char *restrict data;
	size_t w, h;
	unsigned char channels;
	unsigned char bitdepth;
	align_t align_sh;

	unsigned char used_bits;
	enum pix_layout layout:8;
	enum pix_attr attr:8;

	unsigned char rotate;
	bool mirror:1; // Vertical mirror. Horizontal is mirror + 2rotate
	enum alpha_interpretation alpha:2;

	enum image_mode mode:2;
	union {
		struct raster_pal *palette;
		struct image_planes *planes;
	} u;

	float ratio; // Pixel ratio (horizontal_size/vertical_size)
	float dec_scale;

	struct color_space cs;
	struct image_frames *frames;

	struct wu_tree *metadata;
};

struct image_file {
	FILE *ifp;
	size_t nr;
	struct raw_img *sub_img;
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

const char * wu_error_message(enum wu_error err);


struct wu_tree * raw_img_get_metadata(struct raw_img *img);

void raw_img_aspect_ratio(struct raw_img *img, int num, int den);

void raw_img_exif_orientation(struct raw_img *img, int orientation);

enum wu_error raw_img_verify(struct raw_img *img);

bool raw_img_exceeds_limit(const struct raw_img *img,
const struct wu_conf *wuconf);

size_t raw_img_stride(const struct raw_img *img);

size_t raw_img_size(const struct raw_img *img);

bool raw_img_alloc_noverify(struct raw_img *img);

enum wu_error raw_img_alloc(struct raw_img *img);

size_t raw_img_plane_resolve(struct raw_img *img);

void raw_img_plane_subsamp(struct raw_img *img, uint8_t horz, uint8_t vert);

struct image_planes * raw_img_plane_init(struct raw_img *img);

struct raster_pal * raw_img_palette_set(struct raw_img *img,
struct raster_pal *pal);

struct raster_pal * raw_img_palette_init(struct raw_img *img);

int raw_img_frame_prev_keyframe(struct raw_img *img, int current, int i);

void raw_img_frame_set(struct raw_img *img, size_t i, size_t x, size_t y,
size_t w, size_t h, int msec, bool opaque);

size_t raw_img_frames_nr(const struct raw_img *img);

struct image_frames * raw_img_frames_init(struct raw_img *img, size_t nr);

void raw_img_align(struct raw_img *img, uint8_t alignment);

bool raw_img_clone(struct raw_img *dst, struct raw_img *src);

void raw_img_clear(struct raw_img *img);


struct raw_img * realloc_sub_images(struct image_file *file, size_t nr);

struct raw_img * alloc_sub_images(struct image_file *file, size_t nr);

void image_file_free_if_single(struct image_file *file);

void image_file_print(const struct image_file *file, int verbosity);

void image_file_normalize(struct image_file *file);

enum wu_error image_file_total_decoded(struct image_file *file, size_t o);

void image_file_error_append(struct image_file *file, const char *str);

void image_file_status_append(struct image_file *file, enum wu_error status);

void image_file_free(struct image_file *file);


struct raw_img * image_cur_sub_img(const struct image_context *image);

enum image_event image_zoom(struct image_context *image, float new_zoom);

enum image_event image_sub_cycle(struct image_context *image, int steps);

enum image_event image_frame_cycle(struct image_context *image, int steps);

void image_reset(struct image_context *image);

#endif /* WUDEFS */

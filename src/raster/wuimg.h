// SPDX-License-Identifier: 0BSD
#ifndef WUIMG
#define WUIMG

#include <stddef.h>
#include <stdbool.h>

#include "conf.h"
#include "wutree.h"
#include "raster/alpha.h"
#include "raster/bitfield.h"
#include "raster/color.h"
#include "raster/compost.h"
#include "raster/pix.h"
#include "raster/pal.h"
#include "raster/strip.h"

enum wu_error {
	wu_no_change = -1, // For callbacks
	wu_ok = 0,
	wu_alloc_error,
	wu_open_error,
	wu_unknown_file_type,
	wu_unexpected_eof,
	wu_invalid_signature,
	wu_invalid_header,
	wu_unsupported_feature,
	wu_no_image_data,
	wu_exceeds_size_limit,
	wu_int_overflow,
	wu_decoding_error,
	wu_invalid_params,
	wu_display_error,
	wu_unknown_error,
};

struct plane_dim {
	uint8_t subsamp;
	int8_t pos;
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
	image_mode_palette,
	image_mode_planar,
	image_mode_bitfield,
};

struct image_frames {
	size_t nr;
	struct frame_info f[];
};

struct wuimg {
	unsigned char *restrict data;

	size_t w, h;
	unsigned char channels;
	unsigned char bitdepth;
	align_t align_sh;

	unsigned char used_bits;
	enum pix_layout layout:8;
	enum pix_attr attr:8;

	unsigned char rotate; // Clockwise quarter turns
	bool mirror:1; // Vertical mirror. Horizontal is mirror + 2rotate
	enum alpha_interpretation alpha:2;

	bool borrowed:1; // .data is not ours
	bool evolving:1; // Data changes with time
	bool scalable:1; // The decoder will draw according to the window size
	enum image_mode mode:2;
	union {
		struct raster_pal *palette;
		struct image_planes *planes;
		struct bitfield *bitfield;
	} u;

	/* Pixel ratio, the result of horizontal_size/vertical_size.
	 * For a square that is N pixels tall, its width must be N/ratio
	 * pixels for it to look square. */
	float ratio;

	struct color_space cs;
	struct image_frames *frames;

	struct wu_tree *metadata;
};

const char * wu_error_message(enum wu_error err);


struct wu_tree * wuimg_get_metadata(struct wuimg *img);

void wuimg_aspect_ratio(struct wuimg *img, unsigned h_size, unsigned v_size);

void wuimg_exif_orientation(struct wuimg *img, int orientation);

enum wu_error wuimg_verify(struct wuimg *img);

bool wuimg_exceeds_limit(const struct wuimg *img,
const struct wu_conf *wuconf);

size_t wuimg_stride(const struct wuimg *img);

size_t wuimg_size(const struct wuimg *img);

bool wuimg_alloc_noverify(struct wuimg *img);

enum wu_error wuimg_alloc(struct wuimg *img);


struct bitfield * wuimg_bitfield_init(struct wuimg *img);

struct bitfield * wuimg_bitfield_init_from_id(struct wuimg *img,
enum bitfield_id id);


size_t wuimg_plane_resolve(struct wuimg *img);

void wuimg_plane_position(struct wuimg *img, int8_t horz, int8_t vert);

void wuimg_plane_subsamp(struct wuimg *img, uint8_t horz, uint8_t vert);

struct image_planes * wuimg_plane_init(struct wuimg *img);


struct raster_pal * wuimg_palette_set(struct wuimg *img, struct raster_pal *pal);

struct raster_pal * wuimg_palette_init(struct wuimg *img);


int wuimg_frame_prev_keyframe(struct wuimg *img, int current, int i);

bool wuimg_frame_set(struct wuimg *img, size_t i, size_t x, size_t y, size_t w,
size_t h, long sec_num, long sec_den, bool opaque);

size_t wuimg_frames_nr(const struct wuimg *img);

struct image_frames * wuimg_frames_init(struct wuimg *img, size_t nr);


void wuimg_align(struct wuimg *img, uint8_t alignment);

bool wuimg_clone(struct wuimg *dst, struct wuimg *src);

void wuimg_free(struct wuimg *img);

void wuimg_clear(struct wuimg *img);

bool wuimg_has_data(const struct wuimg *img);

size_t wuimg_print(const struct wuimg *img, int verbosity);

#endif /* WUIMG */

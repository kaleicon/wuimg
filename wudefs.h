#ifndef WUDEFS
#define WUDEFS

#include <stdio.h>
#include <stddef.h>
#include <stdbool.h>

#include "common.h"
#include "wutree.h"

#define WU_CANON_NAME "wu"

struct wu_conf {
	// Variable variables. These may be modified at runtime.
	struct display_dims fb; // Framebuffer dimensions.
	unsigned max_img_size; /* Max image size in either dimension. The
		starting value will be capped to the texture size limit. */

	// Const variables
	// Window
	struct display_dims initial_size; // Window size hint on startup.
	unsigned char bg[4]; /* Default window background in RGBA order. If
		Alpha is less than 0xff, a window with a transparent background
		is requested but not guaranteed. RGB values are passed as-is,
		they're not premultiplied by Alpha. */
	enum background_source { // Alternative background sources.
		default_only,    // Use only the default color.
		metadata,        /* If the image format includes a non-black
			background metadata field, use its RGB components with
			the user-defined Alpha, otherwise the default. */
		average,         /* Samples some pixels and gets the average
			color, or, for paletted images, the most used entry. */
		popular,         /* Splits colors in bands and picks the most
			popular. */
		vibrant,         /* Like 'popular', but picks the band with
			the most difference between components instead. */
	} bg_src:3;
	bool no_window_decorations:1; /* Request no decorations or widgets
		around the window. */

	// Animations
	bool cache_frames:1; /* Cache individual frames in memory instead of
		drawing each on top of the previous one. Frames will still be
		decoded as required until a loop is completed. Expect memory
		usage to add up fast. Very not recommended, I forgot why I
		added this. */
	bool anim_space_over_speed:1; /* Render animations as RGB if no alpha
		is needed for any composited frame. This is quick to check and
		will save some memory, but may actually increase the time to
		first display and give lower performance, since CPUs and GPUs
		work better on 4-bytes pieces.
		  Single-frame files are always rendered as RGB if possible.
		Files using a single palette throughout are always rendered in
		paletted mode. */

	// JPEG
	bool jpeg_fast_dct:1; /* Use a faster but less exact DCT algorithm for
		decoding.
		  From libjpeg.txt: "If the JPEG image was compressed using a
		quality level of 85 or below, then there should be little or no
		perceptible difference between the two algorithms."
		  This setting aplies to all images, as there's no simple
		method to determine the quality level. */
	bool jpeg_fast_upsamp:1; /* Use a faster chroma upsampling algorithm.
		  From libjpeg.txt: "The visual impact of the sloppier
		[upsampling] method is often very small."
		  This setting is ignored for certain images to prevent strange
		segfaults. */

	// TIFF
	bool tiff_use_homegrown_unpacker:1; /* Use our own pixel unpacking
		routines instead of libtiff's high-level interface if the image
		fits certain criteria. If applicable, this usually results
		in lower memory usage and faster decoding and display. This
		also allows decoding some exotic bitdepths (32-bit color,
		rgb(a) < 8-bit, etc.) that libtiff doesn't render on its own. */

	// RAW
	bool raw_half_size:1; // Render raw data at half the original size.
	bool raw_prefer_thumbnail:1; /* If true, display the file's embedded
		thumbnail instead if it is at least half as big as the
		original, otherwise do a full and slow render of the raw data.
		The thumbnail is always decoded at full resolution. If the
		thumbnail is a JPEG image, the jpeg decoder function will be
		used and its settings will also apply to it.
		  Note that the thumbnail might differ drastically from the
		interpreted raw data. */

	// SVG
	enum redraw_on { // If and when should the vector be redrawn.
		never = 0,
		upscale, // Only when zooming in.
		anyscale, // When zooming in and out. Maximum crispness always.
	} svg_redraw:2;

	// WEBP
	bool webp_bypass_filtering:1; // Skip the filtering stage for lossy WebP
	bool webp_fast_upsamp:1; // Use a faster chroma upsampler for lossy WebP
	bool webp_use_homegrown_renderer:1; /* Composite animations using our
		own routines instead of libwebp's. They don't seem to be slower,
		but I make no guarantees for correctness. */
};

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

enum pix_packing { // To be used in the bitdepth field
	rgb332 = 3,
	argb1555 = 5,
};

struct wu_cycle {
	int cycle;
	float acc;
};

struct wu_state {
	struct wu_cycle sub;
	enum anim_state {
		anim_playing = 2,
		anim_paused = 3, // For toggling with '^ 1'
	} anim:8;
	enum alpha_state {
		alpha_enabled,
		alpha_checkers,
		alpha_disabled,
	} alpha:8;

	unsigned char rotate;
	bool mirror;

	float x_offset;
	float y_offset;
	float fit_zoom;
	float zoom;
	float dec_scale;
};

enum image_event {
	sub_cycle = 1,
	up_scale = 1 << 1,
	down_scale = 1 << 2,
	scale = up_scale | down_scale,
	move = 1 << 3,
	mirrot = 1 << 4,
};

enum pix_layout { // For programmatic color swizzling
	// r = 0, g = 1, b = 2, a = 3
	gray_alpha = 0x01,
	gray = 0x03,
	rgba =             0x01 << 4 | 0x02 << 2 | 0x03,
	argb = 0x01 << 6 | 0x02 << 4 | 0x03 << 2,
	bgra = 0x02 << 6 | 0x01 << 4 |             0x03,
};

enum pix_attributes {
	pix_normal = 0,
	pix_float = 1,
	pix_inverted = 1 << 1,
	pix_planar = 1 << 2,
};

struct raw_img {
	unsigned char *restrict data;
	unsigned char *restrict palette;
	char *restrict id;

	size_t w, h;

	unsigned char channels;
	unsigned char bitdepth;
	unsigned char true_channels;
	unsigned char alignment;
	enum pix_layout layout:8;
	enum pix_attributes attr:8;

	unsigned char rotate;
	bool mirror; // Vertical mirror. Horizontal is mirror + 2rotate

	int msec;
	float dec_scale;
};

struct image_file {
	FILE *ifp;
	struct raw_img *sub_img;
	size_t nr;
	struct wu_tree metadata;

	void *restrict dec_state; // Used by decoder for callbacks
	int fmt_id;

	enum image_event events;
	unsigned char bg[4];
	bool is_animation;

	char *restrict err_msg;
};

struct image_context {
	const char *name;
	struct image_file file;
	struct wu_conf conf;
	struct wu_state state;
};

const char * wu_error_message(enum wu_error err);

void print_image_information(const struct image_file *file, int verbosity);

void normalize_sub_images(struct image_file *file);

size_t raw_img_addbuf(struct raw_img *img);

void raw_img_clear(struct raw_img *img);

struct raw_img * realloc_sub_images(struct image_file *file, size_t nr);

struct raw_img * alloc_sub_images(struct image_file *file, size_t nr);

void free_image_file(struct image_file *file);

#endif /* WUDEFS */

#ifndef WUDEFS
#define WUDEFS

#include <stdio.h>
#include <stddef.h>
#include <stdbool.h>

#include "common.h"
#include "wutree.h"
#include "raster/pix.h"
#include "raster/pal.h"

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
	enum background_source { // Window background sources.
		bg_default = 0,  // Use only the default color.
		bg_metadata,     /* If the image format includes a non-black
			background metadata field, use its RGB components with
			the user-defined Alpha, otherwise the default. */
		bg_average,      /* Samples some pixels and gets the average
			color, or, for paletted images, the most used entry. */
		bg_popular,      /* Splits colors in bands and picks the most
			popular. */
		bg_vibrant,      /* Like 'popular', but picks the band with
			the most difference between components instead. */
	} bg_src:3;
	bool no_window_decorations:1; /* Request no decorations or widgets
		around the window. */

	// Image decoding
	bool partial_decode:1; /* Decode at the smallest available resolution
		that's bigger than the window when the format allows so. This
		speeds up the time to first display and can make flipping
		through big images actually bearable. A full decode is
		triggered when zooming in.
		  As an aside, this technique is used unconditionally when the
		image dimensions would exceed the maximum image size.
		  Currently applies to JP2, JPEG, and SVG. */

	// Animations
	bool cache_frames:1; /* Cache individual frames in memory instead of
		drawing each on top of the previous one. Frames will still be
		decoded as required until a loop is completed. Expect memory
		usage to add up fast. Very not recommended, I forgot why I
		added this. */
	bool anim_space_over_speed:1; /* Render animations as RGB if no alpha
		is needed for any composited frame. This is quick to check and
		will save some memory, but could actually increase the time to
		first display and give lower performance, as CPUs and GPUs work
		better on 4-byte words.
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
	bool raw_16bit:1; // Render with 16 bits per component instead of 8.
	bool raw_half_size:1; // Render raw data at half the original size.
	bool raw_prefer_thumbnail:1; /* If true, display the file's embedded
		thumbnail instead if it is at least half as big as the
		original, otherwise do a full and slow render of the raw data.
		The thumbnail is always decoded at full resolution. If the
		thumbnail is a JPEG image, the jpeg decoder function will be
		used and so its settings will also apply to it.
		  Note that the thumbnail might differ drastically from the
		interpreted raw data. */

	// SVG
	enum svg_redraw_on { // If and when should the vector be redrawn.
		svg_never = 0,
		svg_upscale, // Only when zooming in.
		svg_anyscale, // When zooming in and out. Crystal crispness always.
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
	bool mirror; // Vertical mirror. Horizontal is mirror + 2rotate
	bool no_alpha;

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

	char *err_msg;
};

struct image_context {
	const char *name;
	struct image_file file;
	int fmt_id;
	struct wu_conf conf;
	struct wu_state state;
};

const char * wu_error_message(enum wu_error err);

void print_image_information(const struct image_file *file, int verbosity);

size_t raw_img_size(struct raw_img *img);

size_t raw_img_addbuf(struct raw_img *img);

void raw_img_clear(struct raw_img *img);

struct raw_img * realloc_sub_images(struct image_file *file, size_t nr);

struct raw_img * alloc_sub_images(struct image_file *file, size_t nr);

void image_file_normalize(struct image_file *file);

void image_file_free(struct image_file *file);

size_t image_fit_factor(const struct wu_conf *conf, size_t w, size_t h,
size_t max, bool partial_decode);

#endif /* WUDEFS */

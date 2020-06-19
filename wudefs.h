#ifndef WUDEFS
#define WUDEFS

#include <stdio.h>
#include <stddef.h>
#include <stdbool.h>

#define WU_CANON_NAME "wu"

struct wu_conf {
	// Window
	int initial_w, initial_h; // Window size hint on startup.
	unsigned int max_img_size; /* Max image size in either dimension. This
		value is capped to the texture size limit. */
	unsigned char bg[4]; /* Window background in RGBA order. Values are NOT
		checked to be premultiplied (that is, it looks funny if any
		value is greater than Alpha). */
	enum background_source { /* For image formats that support some sort of
		'background color' metadata attribute, this setting allows
		using that data as the framebuffer color. */
		default_only = 0, // Only use the user-defined color.
		image_rgb,        /* Use the file's RGB components with the
			user-defined Alpha. */
		image_rgba,       /* Use the file components including Alpha
			(which is usually 0xff). */
	} use_img_bg:8;
	bool no_window_decorations; /* Request no decorations or close widgets
		around the window. */

	// OpenGL
	bool ignore_tex_limit; /* Ignore the max texture size reported by the
		card. Don't complain to me though. */

	// Animations
	bool keep_frames; /* Keep all decoded frames in memory, instead of
		drawing each on top of the previous one. You do not want this,
		even "small" images may use hundreds of megabytes of memory. */

	// JPEG
	bool jpeg_fast_dct; /* Use a faster but less exact DCT algorithm for
		decoding.
		  From libjpeg.txt: "If the JPEG image was compressed using a
		quality level of 85 or below, then there should be little or no
		perceptible difference between the two algorithms."
		  This setting aplies to all images, as there's no simple
		method to determine the quality level. */
	bool jpeg_fast_upsamp; /* From libjpeg.txt: "The visual impact of the
		sloppier [upsampling] method is often very small."
		  This setting is ignored for certain images to prevent strange
		segfaults. */

	// TIFF
	bool tiff_use_homegrown_unpackers; /* Use our own pixel unpacking
		routines instead of libtiff's high-level interface if the
		image fits certain criteria (photometric is in range 0-2). In
		most cases this will result in less memory usage, but it is not
		as well battle tested. These routines also enable decoding some
		exotic bitdepths (32-bit color, rgb(a) < 8-bit, etc.) that
		libtiff doesn't render on its own. */

	// WEBP
	bool webp_bypass_filtering; // Similar to the JPEG options, maybe...
	bool webp_fast_upsamp; // ...and they only apply to lossy WebP.
	bool webp_use_homegrown_renderer; /* Composites animation using our own
		routines instead of libwebp's. This can save memory on
		animations whose final output is verified not to need an alpha
		channel. We wouldn't vouch for its correctness, though. */

	// SVG
	enum antialiasing {
		whatever = 0, // Whatever is cairo's default.
		fast,
		good,
		best,
	} svg_antialiasing:8;
	enum redraw_on { // When should the vector be redrawn.
		never = 0,
		upscale, // Only when zooming in.
		always, // When zooming in and out. (Best quality)
	} svg_redraw:8; // Note: this plays funky with the zoom controls.

	// Runtime variables. These can't be set from the config file.
	unsigned int fb_w, fb_h; // Framebuffer dimensions.
};

enum wu_error {
	wu_ok,
	wu_alloc_error,
	wu_unknown_file_type,
	wu_invalid_params,
	wu_open_error,
	wu_unexpected_eof,
	wu_invalid_signature,
	wu_invalid_header,
	wu_unsupported_feature,
	wu_decoding_error,
	wu_exceeded_size_limit,
	wu_unknown_error,
};

struct wu_state {
	int cycle;
	int cycle_sub_img;
	enum anim_state {
		none = 0,
		playing = 1,
		paused = 3, // For easy toggling with '^ 2'
	} anim:8;

	unsigned char rotate;
	bool mirror;

	float fit_zoom;
	float zoom;
};

enum program_event {
	close_window = 1,
	reload_file = 2,
};

union image_events {
	unsigned char has_event;
	struct {
		bool scale:1;
		bool sub_cycle:1;
	};
};

enum image_event {
	finish = 0,
	scale = 1,
	sub_cycle = 1 << 1,
};

enum remove {
	no_rm = 0,
	warn_rm,
	yes_rm,
};

struct wu_event {
	enum program_event program:8;
	enum image_event image:8;
	enum remove rm:8;
};

enum pix_layout {
	// r = 0, g = 1, b = 2, a = 3
	gray_alpha = 0x01,
	gray = 0x03,
	rgba =             0x01 << 4 | 0x02 << 2 | 0x03,
	bgra = 0x02 << 6 | 0x01 << 4 |             0x03,
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

	bool mirror; // Vertical mirror. Horizontal is mirror + 2rotate
	unsigned char rotate;
	float dec_scale;
	int msec;
};

struct image_file {
	FILE *ifp;
	char *restrict err_msg;
	struct raw_img *sub_img;
	size_t nr;

	enum wu_error (*callback)(struct image_file *file,
		const struct wu_conf *wuconf, struct wu_state *state,
		const enum image_event);
	void *restrict dec_state;
	enum image_event events:8;

	char __private[2];
	bool is_animation;
	unsigned char bg[4];
};

const char * wu_error_message(enum wu_error err);

void print_image_information(const struct image_file *file);

void normalize_sub_images(struct image_file *file);

struct raw_img * realloc_sub_images(struct image_file *file, const size_t nr);

struct raw_img * alloc_sub_images(struct image_file *file, const size_t nr);

void free_image_file(struct image_file *file);

#endif /* WUDEFS */

#ifndef WUDEFS
#define WUDEFS

#include <stdio.h>
#include <stddef.h>
#include <stdbool.h>

#define WU_CANON_NAME "wu"

struct display_dims { // Why would anyone want more than 65535x65535 pixels?...
	unsigned short w, h;
};

struct wu_conf {
	// Variable variables (may be modified at runtime)
	struct display_dims fb; // Framebuffer dimensions.
	unsigned short max_img_size; /* Max image size in either dimension.
		The starting value will be capped to the texture size limit. */

	// Const variables
	// Window
	struct display_dims initial_size; // Window size hint on startup.
	unsigned char bg[4]; /* Default window background in RGBA order. If
		Alpha is less than 0xff, a window with a transparent background
		is requested but not guaranteed. RGB values are passed as-is,
		they're not premultiplied by Alpha. */
	enum background_source { // Alternative background sources.
		default_only,    // Always use the user-defined color.
		metadata,        /* If the image format includes a non-black
			background metadata field, use its RGB components with
			the user-defined Alpha, otherwise the default. */
		average,         /* Gets the average color of the image. For
			paletted images, returns the most used entry. */
		popular,         /* Averages colors by category and picks the
			most popular. */
		vibrant,         /* Like 'popular', but picks the category with
			the most difference between components instead. */
	} bg_src:3;
	bool no_window_decorations:1; /* Request no decorations or widgets
		around the window. */

	// Animations
	bool cache_frames:1; /* Cache animation frames in memory instead of
		drawing each on top of the previous one. Frames will still be
		decoded when required until a loop is completed. Note that
		memory usage may add up very fast. */
	bool anim_space_over_speed:1; /* Render animations as RGB if no alpha
		is needed on any composited frame. This will give somewhat
		lower performance since virtually all GPUs will pad 3-byte data
		to 4-bytes, but might be of some help with 'cache_frames'.
		  Single-frame files are always rendered as RGB if possible. */

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
		fits certain criteria. If applicable, this will result in lower
		memory usage. This also allows decoding some exotic bitdepths
		(32-bit color, rgb(a) < 8-bit, etc.) that libtiff doesn't
		render on its own. */

	// RAW
	bool raw_half_size:1; /* Render raw data at half the original size.
		Gives a nice speedup. */
	bool raw_prefer_thumbnail:1; /* If true, decode the file's embedded
		thumbnail if it is at least half as big as the original,
		otherwise do a full and slow render of the raw data. The
		thumbnail is always decoded at full resolution. If the
		thumbnail is a JPEG image, the jpeg decoder function will be
		used and its settings will also apply to it.
		  Note that the thumbnail might differ drastically from the
		interpreted raw data. */

	// SVG
	enum antialiasing { // Antialiasing quality
		whatever = 0, // Whatever is cairo's default.
		fast,
		good,
		best,
	} svg_antialiasing:2;
	enum redraw_on { // If and when should the vector be redrawn.
		never = 0,
		upscale, // Only when zooming in.
		always, // When zooming in and out.
	} svg_redraw:2; // Note that this plays funky with the zoom controls.

	// WEBP
	bool webp_bypass_filtering:1; // Skip the filtering stage for lossy WebP
	bool webp_fast_upsamp:1; // Use a faster chroma upsampler for lossy WebP
	bool webp_use_homegrown_renderer:1; /* Composite animations using our
		own routines instead of libwebp's. They seem to be faster, but
		may not be correct for all inputs. */
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

enum pix_packing {
	rgb332 = 3,
	bgra4444 = 4,
	bgra5551 = 5,
};

struct wu_pos {
	int cycle;
	float acc;
};

struct wu_state {
	struct wu_pos sub;
	enum anim_state {
		playing = 2,
		paused = 3, // For toggling with '^ 1'
	} anim:8;

	unsigned char rotate;
	bool mirror;

	float x_offset;
	float y_offset;
	float fit_zoom;
	float zoom;
};

enum image_event {
	sub_cycle = 1,
	scale = 1 << 1,
	move = 1 << 2,
	mirrot = 1 << 3,
};

enum pix_layout { // For OpenGL swizzling
	// r = 0, g = 1, b = 2, a = 3
	gray_alpha = 0x01,
	gray = 0x03,
	rgba =             0x01 << 4 | 0x02 << 2 | 0x03,
	argb = 0x01 << 6 | 0x02 << 4 | 0x03 << 2,
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

	bool float_data;
	bool mirror; // Vertical mirror. Horizontal is mirror + 2rotate
	unsigned char rotate;

	int msec;
};

struct image_file {
	FILE *ifp;
	char *restrict err_msg;
	struct raw_img *sub_img;
	size_t nr;
	union {
		FILE *fp;
		char *str;
	} meta;

	void *restrict dec_state;
	enum image_event events:8;

	bool is_animation;
	unsigned char bg[4];
	int fmt_id;
};

const char * wu_error_message(enum wu_error err);

void print_image_information(const struct image_file *file);

void normalize_sub_images(struct image_file *file);

struct raw_img * realloc_sub_images(struct image_file *file, size_t nr);

struct raw_img * alloc_sub_images(struct image_file *file, size_t nr);

void free_image_file(struct image_file *file);

#endif /* WUDEFS */

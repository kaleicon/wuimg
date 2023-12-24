// SPDX-License-Identifier: 0BSD
#ifndef WU_CONF
#define WU_CONF

#include <stdbool.h>

struct display_dims {
	int w, h;
};

struct wu_conf {
	unsigned max_img_size; /* Max image size in either dimension. The
		starting value will be capped to the texture size limit. */
	unsigned magnify_under; /* Auto-magnify images under this size to an
		integer multiple over it. */
	struct display_dims fb; // Framebuffer dimensions. Not seteable.

	// Window
	struct display_dims initial_size; // Window size hint on startup.
	unsigned char bg[4]; /* Default window background in RGBA order. If
		Alpha is less than 0xff, a window with a transparent background
		is requested but not guaranteed. */
	enum background_source { // Window background sources.
		bg_default = 0,  // Use only the default color.
		bg_metadata,     /* If the image format includes a non-black
			background metadata field, use its RGB components with
			the user-defined Alpha, otherwise the default. */
	} bg_src:8;
	bool no_window_decorations; /* Request no decorations or widgets around
		the window. */
	enum heed_ratio {    // When to heed the image pixel ratio.
		heed_always, // Always stretch the image.
		heed_pretty, // Ignore when it would cause unsightly artifacts.
		heed_never, // Never stretch the image.
	} heed_pixel_ratio:8;

	// JPEG
	bool jpeg_fast_dct; /* Use a faster but less exact DCT algorithm for
		decoding.
		  From libjpeg.txt: "If the JPEG image was compressed using a
		quality level of 85 or below, then there should be little or no
		perceptible difference between the two algorithms. When
		decompressing images that were compressed using quality levels
		above 85, however, the difference between JDCT_IFAST and
		JDCT_ISLOW becomes more pronounced. With images compressed
		using quality=97, for instance, JDCT_IFAST incurs generally
		about a 4-6 dB loss (in PSNR) relative to JDCT_ISLOW, but this
		can be larger for some images. If you can avoid it, do not use
		JDCT_IFAST when decompressing images that were compressed using
		quality levels above 97."
		  This setting aplies to all images, as there's no simple
		method to determine the quality level. */

	// TIFF
	bool tiff_use_homegrown_unpacker; /* Use our own pixel unpacking
		routines instead of libtiff's high-level interface if the image
		fits certain criteria. Where applicable, this usually results
		in lower memory usage, faster decoding and display, and better
		support for some exotic bitdepths. */

	// RAW
	bool raw_16bit; // Render with 16 bits per component instead of 8.
	bool raw_half_size; // Render at half the original size.
	bool raw_prefer_thumbnail; /* If true, display the file's embedded
		thumbnail instead if it is at least half as big as the
		original, otherwise do a full and slow render of the raw data.
		If the thumbnail is a JPEG image, the jpeg decoder function
		will be used and so its settings will also apply to it.
		  Note that the thumbnail might have camera effects applied,
		and so might differ drastically from a straight render of the
		raw data. */

	// SVG
	enum svg_redraw_on { // If and when should the vector be redrawn.
		svg_never = 0,
		svg_upscale, // Only when zooming in.
		svg_scale, // When zooming in and out. Crystal crispness always.
	} svg_redraw:8;

	// WEBP
	bool webp_bypass_filtering; // Skip the filtering stage for lossy WebP.
	bool webp_fast_upsamp; /* Use a faster chroma upsampler for lossy WebP.
		This only applies to lossy animations, as static images are
		upsampled in the GPU. */
	bool webp_use_homegrown_renderer; /* Composite animation frames using
		our own routines instead of libwebp's. They seem to be slightly
		faster. */
};

struct wu_conf conf_default(void);

struct wu_conf conf_load(void);

#endif /* WU_CONF */

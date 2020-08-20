#include "wudefs.h"

struct wu_conf default_config() {
	return (struct wu_conf) {
		// Window
		.initial_size = {1280, 960},

		.bg[0] = 0x11,
		.bg[1] = 0x11,
		.bg[2] = 0x11,
		.bg[3] = 0x33,

//		.bg_src = metadata,
		.bg_src = popular,

		// Animations
//		.cache_frames = true,
//		.anim_space_over_speed = true,

		// JPEG
		.jpeg_fast_dct = true,
		.jpeg_fast_upsamp = true,

		// TIFF
		.tiff_use_homegrown_unpacker = true,

		// RAW
//		.raw_prefer_thumbnail = true,
		.raw_half_size = true,

		// SVG
		.svg_antialiasing = best,
		.svg_redraw = upscale,

		// WEBP
		.webp_bypass_filtering = true,
		.webp_fast_upsamp = true,
		.webp_use_homegrown_renderer = true,
	};
}

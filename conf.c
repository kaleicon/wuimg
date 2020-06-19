// This file is directly included from main.c, it was just splitted apart for
// convenience.
// See the wu_conf struct in wudefs.h for explanations. The definitions are
// such that the default values for everything happen to be 0 (ergo, false).
static void set_user_conf(struct wu_conf *conf) {
	memset(conf, 0, sizeof(struct wu_conf));

	// Window
	conf->initial_w = 1280;
	conf->initial_h = 960;
	conf->no_window_decorations = true;
	conf->bg[0] = 0x11;
	conf->bg[1] = 0x11;
	conf->bg[2] = 0x11;
	conf->bg[3] = 0x33;
	conf->use_img_bg = image_rgb;

	// Animations
//	conf->keep_frames = true;

	// JPEG
	conf->jpeg_fast_dct = true;
	conf->jpeg_fast_upsamp = true;

	// TIFF
	conf->tiff_use_homegrown_unpackers = true;

	// WEBP
	conf->webp_bypass_filtering = true;
	conf->webp_fast_upsamp = true;
	conf->webp_use_homegrown_renderer = true;

	// SVG
	conf->svg_antialiasing = best;
	conf->svg_redraw = always;
}

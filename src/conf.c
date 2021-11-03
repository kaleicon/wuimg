#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>

#include <unistd.h>
#include <fcntl.h>
#include <pwd.h>

#include "conf.h"
#include "common.h"
#include "raster/text.h"

struct wu_conf conf_default(void) {
	const unsigned default_max = USHRT_MAX / 4;
	return (struct wu_conf) {
		.fb = {default_max, default_max},
		.max_img_size = default_max,

		// Window
		.initial_size = {640, 480},

		.bg[0] = 0x33,
		.bg[1] = 0x33,
		.bg[2] = 0x33,
		.bg[3] = 0x66,

		.bg_src = bg_metadata,

		// Decoding
		.partial_decode = true,

		// JPEG
		.jpeg_fast_dct = true,
		.jpeg_fast_upsamp = true,

		// TIFF
		.tiff_use_homegrown_unpacker = true,

		// SVG
		.svg_redraw = svg_upscale,

		// WEBP
		.webp_bypass_filtering = true,
		.webp_fast_upsamp = true,
		.webp_use_homegrown_renderer = true,
	};
}

static bool read_bool(struct text_parser *tp, bool *ok) {
	struct wustr val = text_get_word(tp);
	if (wustr_eq_str(val, "true")) {
		return true;
	} else if (wustr_eq_str(val, "false")) {
		return false;
	}
	*ok = false;
	return false;
}

static bool parse_config_file(struct wu_conf *conf, struct text_parser *tp) {
	while (tp->pos < tp->len) {
		text_skip_space(tp);
		struct wustr key = text_get_word(tp);
		if (key.len == 0 || key.str[0] == '#') {
			text_skip_line(tp);
			continue;
		}
		if (text_next_nonblank(tp) != '=') {
			return false;
		}

		text_skip_blank(tp);
		bool ok = true;
		if (wustr_eq_str(key, "max_img_size")) {
			ok = text_scan_uint_MACRO(tp, &conf->max_img_size);
		} else if (wustr_eq_str(key, "initial_size")) {
			struct display_dims *i = &conf->initial_size;
			ok = text_scan_uint_MACRO(tp, &i->w);
			if (ok) {
				text_skip_blank(tp);
				ok = text_scan_uint_MACRO(tp, &i->h);
			}
		} else if (wustr_eq_str(key, "bg")) {
			unsigned char *bg = conf->bg;
			for (size_t i = 0; ok && i < ARRAY_LEN(conf->bg); ++i) {
				text_skip_blank(tp);
				ok = text_scan_xint_MACRO(tp, bg + i);
			}
		} else if (wustr_eq_str(key, "bg_src")) {
			struct wustr val = text_get_word(tp);
			text_skip_blank(tp);
			if (wustr_eq_str(val, "default")) {
				conf->bg_src = bg_default;
			} else if (wustr_eq_str(val, "metadata")) {
				conf->bg_src = bg_metadata;
			} else if (wustr_eq_str(val, "average")) {
				conf->bg_src = bg_average;
			} else if (wustr_eq_str(val, "popular")) {
				conf->bg_src = bg_popular;
			} else if (wustr_eq_str(val, "vibrant")) {
				conf->bg_src = bg_vibrant;
			} else {
				ok = false;
			}
		} else if (wustr_eq_str(key, "no_window_decorations")) {
			conf->no_window_decorations = read_bool(tp, &ok);

		} else if (wustr_eq_str(key, "partial_decode")) {
			conf->partial_decode = read_bool(tp, &ok);

		} else if (wustr_eq_str(key, "cache_frames")) {
			conf->cache_frames = read_bool(tp, &ok);
		} else if (wustr_eq_str(key, "anim_space_over_speed")) {
			conf->anim_space_over_speed = read_bool(tp, &ok);

		} else if (wustr_eq_str(key, "jpeg_fast_dct")) {
			conf->jpeg_fast_dct = read_bool(tp, &ok);
		} else if (wustr_eq_str(key, "jpeg_fast_upsamp")) {
			conf->jpeg_fast_upsamp = read_bool(tp, &ok);

		} else if (wustr_eq_str(key, "tiff_use_homegrown_unpacker")) {
			conf->tiff_use_homegrown_unpacker = read_bool(tp, &ok);

		} else if (wustr_eq_str(key, "raw_half_size")) {
			conf->raw_half_size = read_bool(tp, &ok);
		} else if (wustr_eq_str(key, "raw_16bit")) {
			conf->raw_16bit = read_bool(tp, &ok);
		} else if (wustr_eq_str(key, "raw_prefer_thumbnail")) {
			conf->raw_prefer_thumbnail = read_bool(tp, &ok);

		} else if (wustr_eq_str(key, "svg_redraw")) {
			struct wustr val = text_get_word(tp);
			text_skip_blank(tp);
			if (wustr_eq_str(val, "never")) {
				conf->svg_redraw = svg_never;
			} else if (wustr_eq_str(val, "upscale")) {
				conf->svg_redraw = svg_upscale;
			} else if (wustr_eq_str(val, "anyscale")) {
				conf->svg_redraw = svg_anyscale;
			} else {
				ok = false;
			}

		} else if (wustr_eq_str(key, "webp_bypass_filtering")) {
			conf->webp_bypass_filtering = read_bool(tp, &ok);
		} else if (wustr_eq_str(key, "webp_fast_upsamp")) {
			conf->webp_fast_upsamp = read_bool(tp, &ok);
		} else if (wustr_eq_str(key, "webp_use_homegrown_renderer")) {
			conf->webp_use_homegrown_renderer = read_bool(tp, &ok);
		} else {
			ok = false;
		}

		if (!ok) {
			return false;
		}

		text_skip_blank(tp);
		switch (text_next_char(tp)) {
		case EOF:
			return true;
		case '\n':
			break;
		case '#':
			text_skip_line(tp);
			break;
		default:
			return false;
		}
	}
	return true;
}

static int try_path(const char *dirname, const char *filename) {
	int fd = -1;
	if (dirname && dirname[0] == '/') {
		const int dir = open(dirname, O_RDONLY | O_DIRECTORY);
		if (dir != -1) {
			fd = openat(dir, filename, O_RDONLY);
			close(dir);
		}
	}
	return fd;
}

static int get_config_fd(void) {
	int fd = try_path(getenv("XDG_CONFIG_HOME"), "wu.conf");
	if (fd != -1) {
		return fd;
	}

	const char config_wu[] = ".config/wu.conf";
	fd = try_path(getenv("HOME"), config_wu);
	if (fd != -1) {
		return fd;
	}

	struct passwd *pw = getpwuid(getuid());
	if (pw) {
		fd = try_path(pw->pw_dir, config_wu);
	}
	return fd;
}

struct wu_conf conf_load(void) {
	struct wu_conf conf = conf_default();
	const int fd = get_config_fd();
	if (fd == -1) {
		return conf;
	}

	struct mmap_info mm;
	bool ok = mmap_file_fd(&mm, fd);
	close(fd);
	if (!ok) {
		return conf;
	}

	struct text_parser tp;
	text_parser_mem(&tp, mm.len, mm.data);

	ok = parse_config_file(&conf, &tp);
	munmap_file(mm);
	if (ok) {
		return conf;
	}
	puts("Failed to parse config file. Using defaults.");
	return conf_default();
}

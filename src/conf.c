#include <stdlib.h>
#include <ctype.h>
#include <limits.h>

#include <unistd.h>
#include <fcntl.h>
#include <pwd.h>
#include <sys/mman.h>

#include "conf.h"
#include "common.h"
#include "raster/memparser.h"

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

static bool read_xint(struct mp_parser *tp, unsigned char *cval) {
	long val;
	const unsigned char limit = UCHAR_MAX;
	if (mp_get_xint(tp, sizeof(*cval) * 3, &val) && val <= limit) {
		*cval = (unsigned char)val;
		return true;
	}
	return false;
}

static bool read_uint(struct mp_parser *tp, void *ival) {
	long val;
	const int limit = INT_MAX;
	if (mp_get_uint(tp, sizeof(limit) * 3, &val) && val <= limit) {
		*(int *)ival = (int)val;
		return true;
	}
	return false;
}

static bool read_bool(struct mp_parser *tp, bool *ok) {
	struct wuptr val = mp_get_word(tp);
	if (wuptr_eq_str(val, "true")) {
		return true;
	} else if (wuptr_eq_str(val, "false")) {
		return false;
	}
	*ok = false;
	return false;
}

static bool parse_config_file(struct wu_conf *conf, struct mp_parser *tp) {
	while (tp->pos < tp->len) {
		mp_skip_space(tp);
		struct wuptr key = mp_get_word(tp);
		if (key.len == 0 || key.ptr[0] == '#') {
			mp_skip_line(tp);
			continue;
		}
		if (mp_next_nonblank(tp) != '=') {
			return false;
		}

		mp_skip_blank(tp);
		bool ok = true;
		if (wuptr_eq_str(key, "max_img_size")) {
			ok = read_uint(tp, &conf->max_img_size);
		} else if (wuptr_eq_str(key, "initial_size")) {
			struct display_dims *i = &conf->initial_size;
			ok = read_uint(tp, &i->w);
			if (ok) {
				mp_skip_blank(tp);
				ok = read_uint(tp, &i->h);
			}
		} else if (wuptr_eq_str(key, "bg")) {
			unsigned char *bg = conf->bg;
			for (size_t i = 0; ok && i < ARRAY_LEN(conf->bg); ++i) {
				mp_skip_blank(tp);
				ok = read_xint(tp, bg + i);
			}
		} else if (wuptr_eq_str(key, "bg_src")) {
			struct wuptr val = mp_get_word(tp);
			mp_skip_blank(tp);
			if (wuptr_eq_str(val, "default")) {
				conf->bg_src = bg_default;
			} else if (wuptr_eq_str(val, "metadata")) {
				conf->bg_src = bg_metadata;
			} else {
				ok = false;
			}
		} else if (wuptr_eq_str(key, "no_window_decorations")) {
			conf->no_window_decorations = read_bool(tp, &ok);

		} else if (wuptr_eq_str(key, "partial_decode")) {
			conf->partial_decode = read_bool(tp, &ok);

		} else if (wuptr_eq_str(key, "anim_space_over_speed")) {
			conf->anim_space_over_speed = read_bool(tp, &ok);

		} else if (wuptr_eq_str(key, "jpeg_fast_dct")) {
			conf->jpeg_fast_dct = read_bool(tp, &ok);

		} else if (wuptr_eq_str(key, "tiff_use_homegrown_unpacker")) {
			conf->tiff_use_homegrown_unpacker = read_bool(tp, &ok);

		} else if (wuptr_eq_str(key, "raw_half_size")) {
			conf->raw_half_size = read_bool(tp, &ok);
		} else if (wuptr_eq_str(key, "raw_16bit")) {
			conf->raw_16bit = read_bool(tp, &ok);
		} else if (wuptr_eq_str(key, "raw_prefer_thumbnail")) {
			conf->raw_prefer_thumbnail = read_bool(tp, &ok);

		} else if (wuptr_eq_str(key, "svg_redraw")) {
			struct wuptr val = mp_get_word(tp);
			mp_skip_blank(tp);
			if (wuptr_eq_str(val, "never")) {
				conf->svg_redraw = svg_never;
			} else if (wuptr_eq_str(val, "upscale")) {
				conf->svg_redraw = svg_upscale;
			} else if (wuptr_eq_str(val, "anyscale")) {
				conf->svg_redraw = svg_anyscale;
			} else {
				ok = false;
			}

		} else if (wuptr_eq_str(key, "webp_bypass_filtering")) {
			conf->webp_bypass_filtering = read_bool(tp, &ok);
		} else if (wuptr_eq_str(key, "webp_fast_upsamp")) {
			conf->webp_fast_upsamp = read_bool(tp, &ok);
		} else if (wuptr_eq_str(key, "webp_use_homegrown_renderer")) {
			conf->webp_use_homegrown_renderer = read_bool(tp, &ok);
		} else {
			ok = false;
		}

		if (!ok) {
			return false;
		}

		mp_skip_blank(tp);
		switch (mp_next_char(tp)) {
		case EOF:
			return true;
		case '\n':
			break;
		case '#':
			mp_skip_line(tp);
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

	struct map_info mm;
	bool ok = map_file_fd(&mm, fd);
	close(fd);
	if (!ok) {
		return conf;
	}

	struct mp_parser tp = mp_parser_mem(mm.len, mm.data);

	ok = parse_config_file(&conf, &tp);
	unmap_file(&mm);
	if (ok) {
		return conf;
	}
	puts("Failed to parse config file. Using defaults.");
	return conf_default();
}

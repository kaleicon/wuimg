// SPDX-License-Identifier: 0BSD
#include <ctype.h>
#include <limits.h>
#include <stdlib.h>

#include <unistd.h>
#include <fcntl.h>
#include <pwd.h>

#include "conf.h"
#include "term.h"
#include "misc/file.h"
#include "misc/mparser.h"

static const unsigned DEFAULT_MAX = USHRT_MAX / 4;

struct enum_str {
	const char *str;
	unsigned char val;
};

struct wu_conf conf_default(void) {
	return (struct wu_conf) {
		.max_img_size = DEFAULT_MAX,

		// Window
		.initial_size = {640, 480},
		.bg = {0x33, 0x33, 0x33, 0x66},
		.bg_src = bg_metadata,

		// JPEG
		.jpeg_fast_dct = true,

		// TIFF
		.tiff_use_homegrown_unpacker = true,

		// WEBP
		.webp_bypass_filtering = true,
		.webp_fast_upsamp = true,
		.webp_use_homegrown_renderer = true,
	};
}

static struct wu_conf sanitize_conf(struct wu_conf conf) {
	if (conf.initial_size.w < 1 || conf.initial_size.h < 1) {
		conf.initial_size = (struct display_dims){640, 480};
	}
	if (conf.max_img_size < 1) {
		conf.max_img_size = DEFAULT_MAX;
	}
	return conf;
}

static long read_xint(struct mparser *tp, bool *ok) {
	long val;
	*ok = mp_scan_xint(tp, 5, &val);
	return val;
}

static bool read_bool(struct mparser *tp, bool *ok) {
	struct wuptr val = mp_next_word(tp);
	if (wuptr_eq_str(val, "true")) {
		return true;
	} else if (wuptr_eq_str(val, "false")) {
		return false;
	}
	*ok = false;
	return false;
}

static unsigned char read_enum(struct mparser *tp, bool *ok,
const struct enum_str *e, const size_t len) {
	struct wuptr val = mp_next_word(tp);
	mp_skip_blank(tp);
	for (size_t i = 0; i < len; ++i) {
		if (wuptr_eq_str(val, e[i].str)) {
			return e[i].val;
		}
	}
	*ok = false;
	return 0;
}

static bool parse_config_file(struct wu_conf *conf, struct mparser *tp) {
	while (tp->pos < tp->len) {
		mp_skip_space(tp);
		struct wuptr key = mp_next_word(tp);
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
			conf->max_img_size = (unsigned)read_xint(tp, &ok);
		} else if (wuptr_eq_str(key, "magnify_under")) {
			conf->magnify_under = (unsigned)read_xint(tp, &ok);
		} else if (wuptr_eq_str(key, "initial_size")) {
			struct display_dims *i = &conf->initial_size;
			i->w = (int)read_xint(tp, &ok);
			if (ok) {
				mp_skip_blank(tp);
				i->h = (int)read_xint(tp, &ok);
			}
		} else if (wuptr_eq_str(key, "bg")) {
			unsigned char *bg = conf->bg;
			for (size_t i = 0; ok && i < ARRAY_LEN(conf->bg); ++i) {
				mp_skip_blank(tp);
				bg[i] = (unsigned char)read_xint(tp, &ok);
			}
		} else if (wuptr_eq_str(key, "bg_src")) {
			const struct enum_str e[] = {
				{"default", bg_default},
				{"metadata", bg_metadata},
			};
			conf->bg_src = read_enum(tp, &ok, e, ARRAY_LEN(e));
		} else if (wuptr_eq_str(key, "no_window_decorations")) {
			conf->no_window_decorations = read_bool(tp, &ok);
		} else if (wuptr_eq_str(key, "heed_pixel_ratio")) {
			const struct enum_str e[] = {
				{"always", heed_always},
				{"pretty", heed_pretty},
				{"never", heed_never},
			};
			conf->heed_pixel_ratio = read_enum(tp, &ok, e, ARRAY_LEN(e));

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

		} else if (wuptr_eq_str(key, "svg_window_adapt")) {
			conf->svg_window_adapt = read_bool(tp, &ok);

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
		case '#':
			mp_skip_line(tp);
			break;
		case '\n':
			break;
		case EOF:
			return true;
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
		if (dir >= 0) {
			fd = openat(dir, filename, O_RDONLY);
			close(dir);
		}
	}
	return fd;
}

static int get_config_fd(void) {
	int fd = try_path(getenv("XDG_CONFIG_HOME"), "wu.conf");
	if (fd >= 0) {
		return fd;
	}

	const char config_wu[] = ".config/wu.conf";
	fd = try_path(getenv("HOME"), config_wu);
	if (fd >= 0) {
		return fd;
	}

	struct passwd *pw = getpwuid(getuid());
	if (pw) {
		fd = try_path(pw->pw_dir, config_wu);
	}
	return fd;
}

static bool open_config(struct map_info *mm) {
	bool ok = false;
	const int fd = get_config_fd();
	if (fd >= 0) {
		ok = file_map_fd(mm, fd);
		close(fd);
	}
	return ok;
}

struct wu_conf conf_load(void) {
	struct wu_conf conf = conf_default();
	struct map_info mm;
	if (!open_config(&mm)) {
		return conf;
	}

	struct mparser tp = mp_map(mm);
	const bool ok = parse_config_file(&conf, &tp);
	file_unmap(&mm);
	if (ok) {
		return sanitize_conf(conf);
	}
	term_line_put("Failed to parse config file. Using defaults.", stderr);
	return conf_default();
}

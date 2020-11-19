#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "wudefs.h"
#include "common.h"

struct wu_conf default_config(void) {
	return (struct wu_conf) {
		// Window
		.initial_size = {960, 720},

		.bg[0] = 0x11,
		.bg[1] = 0x11,
		.bg[2] = 0x11,
		.bg[3] = 0x66,

		.bg_src = metadata,

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
//		.raw_half_size = true,

		// SVG
		.svg_redraw = upscale,

		// WEBP
		.webp_bypass_filtering = true,
		.webp_fast_upsamp = true,
		.webp_use_homegrown_renderer = true,
	};
}

static bool read_bool(FILE *cfp, bool *fail) {
	char val[6];
	if (fscanf(cfp, "%5s", val)) {
		if (!strcmp("true", val)) {
			return true;
		} else if (!strcmp("false", val)) {
			return false;
		}
	}
	*fail = true;
	return false;
}

static bool parse_config_file(struct wu_conf *conf, FILE *cfp) {
	char key[40];
	while (fscanf(cfp, " %39s", key) == 1) {
		if (key[0] == '#') {
			skip_line(cfp);
			continue;
		}

		char dummy_match;
		if (fscanf(cfp, " = %c", &dummy_match) != 1) {
			return false;
		}

		ungetc(dummy_match, cfp);
		bool fail = false;
		if (!strcmp("max_img_size", key)) {
			if (fscanf(cfp, "%u", &conf->max_img_size) != 1) {
				return false;
			}
		} else if (!strcmp("initial_size", key)) {
			struct display_dims *i = &conf->initial_size;
			if (fscanf(cfp, "%u %u", &i->w, &i->h) != 2) {
				return false;
			}
		} else if (!strcmp("bg", key)) {
			unsigned char *bg = conf->bg;
			const int m = fscanf(cfp, "%hhx %hhx %hhx %hhx",
				bg, bg + 1, bg + 2, bg + 3);
			if (m != 4) {
				return false;
			}
		} else if (!strcmp("bg_src", key)) {
			char val[16];
			if (fscanf(cfp, "%15s", val) != 1) {
				return false;
			}
			if (!strcmp("default_only", val)) {
				conf->bg_src = default_only;
			} else if (!strcmp("metadata", val)) {
				conf->bg_src = metadata;
			} else if (!strcmp("average", val)) {
				conf->bg_src = average;
			} else if (!strcmp("popular", val)) {
				conf->bg_src = popular;
			} else if (!strcmp("vibrant", val)) {
				conf->bg_src = vibrant;
			} else {
				return false;
			}
		} else if (!strcmp("no_window_decorations", key)) {
			conf->no_window_decorations = read_bool(cfp, &fail);
		} else if (!strcmp("cache_frames", key)) {
			conf->cache_frames = read_bool(cfp, &fail);
		} else if (!strcmp("anim_space_over_speed", key)) {
			conf->anim_space_over_speed = read_bool(cfp, &fail);

		} else if (!strcmp("jpeg_fast_dct", key)) {
			conf->jpeg_fast_dct = read_bool(cfp, &fail);
		} else if (!strcmp("jpeg_fast_upsamp", key)) {
			conf->jpeg_fast_upsamp = read_bool(cfp, &fail);

		} else if (!strcmp("tiff_use_homegrown_unpacker", key)) {
			conf->tiff_use_homegrown_unpacker = read_bool(cfp,
				&fail);

		} else if (!strcmp("raw_half_size", key)) {
			conf->raw_half_size = read_bool(cfp, &fail);
		} else if (!strcmp("raw_prefer_thumbnail", key)) {
			conf->raw_prefer_thumbnail = read_bool(cfp, &fail);

		} else if (!strcmp("svg_redraw", key)) {
			char val[9];
			if (fscanf(cfp, "%8s", val) != 1) {
				return false;
			}
			if (!strcmp("never", val)) {
				conf->svg_redraw = never;
			} else if (!strcmp("upscale", val)) {
				conf->svg_redraw = upscale;
			} else if (!strcmp("anyscale", val)) {
				conf->svg_redraw = anyscale;
			} else {
				return false;
			}

		} else if (!strcmp("webp_bypass_filtering", key)) {
			conf->webp_bypass_filtering = read_bool(cfp, &fail);
		} else if (!strcmp("webp_fast_upsamp", key)) {
			conf->webp_fast_upsamp = read_bool(cfp, &fail);
		} else if (!strcmp("webp_use_homegrown_renderer", key)) {
			conf->webp_use_homegrown_renderer = read_bool(cfp,
				&fail);
		} else {
			return false;
		}

		if (fail) {
			return false;
		}

		for (;;) {
			const int c = getc(cfp);
			if (c == '#') {
				skip_line(cfp);
				break;
			} else if (c == '\n') {
				break;
			} else if (c == EOF) {
				return true;
			} else if (isspace(c)) {
				continue;
			} else {
				return false;
			}
		}
	}
	return true;
}

struct wu_conf load_config(void) {
	struct wu_conf conf = default_config();

	char *filename;
	const char name[] = "wu.conf";
	const char *envconf = getenv("XDG_CONFIG_HOME");
	if (envconf && envconf[0] == '/') {
		const size_t eclen = strlen(envconf);
		filename = malloc(eclen + 1 /* '/' */+ sizeof(name));
		if (!filename) {
			return conf;
		}

		memcpy(filename, envconf, eclen);
		filename[eclen] = '/';
		memcpy(filename + eclen + 1, name, sizeof(name));
	} else {
		const char dir[] = "/.config/";
		const char *home = getenv("HOME");
		if (!home || home[0] != '/') {
			return conf;
		}

		const size_t hlen = strlen(home);
		const size_t dlen = sizeof(dir) - 1;
		filename = malloc(hlen + dlen + sizeof(name));
		if (!filename) {
			return conf;
		}
		memcpy(filename, home, hlen);
		memcpy(filename + hlen, dir, dlen);
		memcpy(filename + hlen + dlen, name, sizeof(name));
	}

	FILE *cfp = fopen(filename, "rb");
	free(filename);
	if (!cfp) {
		return conf;
	}

	const bool success = parse_config_file(&conf, cfp);
	fclose(cfp);
	if (success) {
		return conf;
	}
	puts("Failed to parse config file. Using defaults.");
	return default_config();
}

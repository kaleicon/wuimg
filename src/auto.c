#include "misc/common.h"
#include "raster/fmt.h"
#include "auto.h"

#define AUTO_CSTR(arr) .size = (uint8_t)(sizeof(arr) - 1), .bytes = (const uint8_t *)arr

#define AUTO_READ(rdesc) .rlen = (uint8_t)(ARRAY_LEN(rdesc)), .read = rdesc

// AVS
static const struct auto_read avs_read[] = {
	{'w', 4},
	{'h', 4},
};
const struct auto_desc avs_desc = {
	.channels = 4, .bitdepth = 8, .layout = pix_argb,
	.endian = big_endian,
	AUTO_READ(avs_read),
};

// BRU - Degas Elite Brush
const struct auto_desc bru_desc = {
	.w = 8, .h = 8,
	.channels = 1, .bitdepth = 8,
	.used_bits = 1, .attr = pix_inverted,
};

// DOO (Atari Doodle)
const struct auto_desc doo_desc = {
	.w = 640, .h = 400,
	.channels = 1, .bitdepth = 1,
	.attr = pix_inverted,
};

// FARBFELD
static const struct auto_read farbfeld_read[] = {
	{auto_match, AUTO_CSTR("farbfeld")},
	{'w', 4},
	{'h', 4},
};
const struct auto_desc farbfeld_desc = {
	.channels = 4, .bitdepth = 16,
	.endian = big_endian,
	AUTO_READ(farbfeld_read),
};

/* Atari Falcon True Color family */
// COKE
static const struct auto_read coke_read[] = {
	{auto_match, AUTO_CSTR("COKE format.")},
	{'w', 2},
	{'h', 2},
	{auto_match, AUTO_CSTR("\x00\x12")}, // Offset to raster, always 0x0012
};
const struct auto_desc coke_desc = {
	.channels = 1, .bitdepth = 16,
	.layout = pix_bgra, .bitfield = 0x565,
	.endian = big_endian,
	AUTO_READ(coke_read),
};
// EggPaint
static const struct auto_read eggpaint_read[] = {
	{auto_match, AUTO_CSTR("TRUP")},
	{'w', 2},
	{'h', 2},
};
const struct auto_desc eggpaint_desc = {
	.channels = 1, .bitdepth = 16,
	.layout = pix_bgra, .bitfield = 0x565,
	.endian = big_endian,
	AUTO_READ(eggpaint_read),
};
// FTC (Falcon True Color)
const struct auto_desc ftc_desc = {
	.w = 384, .h = 240,
	.channels = 1, .bitdepth = 16,
	.layout = pix_bgra, .bitfield = 0x565,
};
// GodPaint
static const struct auto_read god_read[] = {
	{auto_skip, 2},
	{'w', 2},
	{'h', 2},
};
const struct auto_desc god_desc = {
	.channels = 1, .bitdepth = 16,
	.layout = pix_bgra, .bitfield = 0x565,
	.endian = big_endian,
	AUTO_READ(god_read),
};
// Spooky Sprites TRP
static const struct auto_read trp_read[] = {
	{auto_match, AUTO_CSTR("tru?")},
	{'w', 2},
	{'h', 2},
};
const struct auto_desc trp_desc = {
	.channels = 1, .bitdepth = 16,
	.layout = pix_bgra, .bitfield = 0x565,
	.endian = big_endian,
	AUTO_READ(trp_read),
};

enum wu_error auto_load(struct image_file *infile, const struct auto_desc *desc) {
	struct wuimg *img = infile->sub_img;
	const enum wu_error st = wuimg_alloc(img);
	if (st == wu_ok) {
		return fmt_load_raster_swap(img, infile->ifp, desc->endian)
			? wu_ok : wu_unexpected_eof;
	}
	return st;
}

static void set_value(struct wuimg *img, const enum auto_dst dst,
const uint32_t val) {
	switch (dst) {
	case auto_width: img->w = val; return;
	case auto_height: img->h = val; return;
	case auto_skip: return;
	case auto_match:
		break;
	}
	fatal_bug(__func__, "Nowhere to put value");
}

enum wu_error auto_init(struct image_file *infile, const struct wu_conf *conf,
const struct auto_desc *desc) {
	struct wuimg *img = alloc_sub_images(infile, 1);
	if (!img) {
		return wu_alloc_error;
	}

	img->w = desc->w;
	img->h = desc->h;
	img->channels = desc->channels;
	img->bitdepth = desc->bitdepth;
	img->used_bits = desc->used_bits;
	img->layout = desc->layout;
	img->attr = desc->attr;
	if (desc->bitfield) {
		if (!wuimg_bitfield_init_from_id(img, desc->bitfield)) {
			return wu_alloc_error;
		}
	}

	if (desc->rlen) {
		size_t read = 0;
		for (uint8_t r = 0; r < desc->rlen; ++r) {
			read += desc->read[r].size;
		}

		uint8_t buf[18];
		if (read > sizeof(buf)) {
			fatal_bug(__func__, "Buffer is too small");
		}

		if (!fread(buf, read, 1, infile->ifp)) {
			return wu_unexpected_eof;
		}

		size_t pos = 0;
		for (uint8_t r = 0; r < desc->rlen; ++r) {
			const struct auto_read *dr = desc->read + r;
			if (dr->dst == auto_match) {
				if (memcmp(buf + pos, dr->bytes, dr->size)) {
					return wu_invalid_header;
				}
			} else {
				uint32_t val;
				switch (dr->size) {
				case 1:
					val = buf[pos];
					break;
				case 2:
					val = buf_endian16(buf + pos, desc->endian);
					break;
				case 4:
					val = buf_endian32(buf + pos, desc->endian);
					break;
				default:
					fatal_bug(__func__, "Unexpected word size");
					// Fix uninitialized warnings
					return wu_invalid_params;
				}
				set_value(img, dr->dst, val);
			}
			pos += dr->size;
		}
	}
	return wuimg_exceeds_limit(img, conf) ? wu_exceeds_size_limit : wu_ok;
}

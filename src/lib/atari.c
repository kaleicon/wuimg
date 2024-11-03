// SPDX-License-Identifier: 0BSD
#include "misc/common.h"
#include "misc/decomp.h"
#include "misc/endian.h"
#include "misc/math.h"
#include "misc/mem.h"
#include "raster/graphics_adapters.h"
#include "raster/fmt.h"

#include "lib/atari.h"

/* In the Atari ST, the bits for each 16 pixel run are stored in a planar
 * format, with each bitplane kept in a 16-bit big-endian word. The number
 * of planes/words is the same as the image bitdepth.
 *
 * If you are in Low Resolution mode, which is 320x200 with a bitdepth of 4,
 * memory looks like this:

	.--------------- Pixels 0 to 15 --------------.  .-- Pixels 16 to 31...
	+---------+ +---------+ +---------+ +---------+  +---------+ +----
	| Plane 0 | | Plane 1 | | Plane 2 | | Plane 3 |  | Plane 0 | | ...
	+---------+ +---------+ +---------+ +---------+  +---------+ +----

 * Plane 0 contains the least-significant bits of the 16-pixel group.
 * Plane 1 the second least-significant bits, and so on.
 * Higher bits correspond to the leftmost pixels. So Bit 15 of Plane 0 is the
 * lower bit of Pixel 0, Bit 14 the lower bit of Pixel 1, etc.

http://www.bitsavers.org/pdf/atari/ST/Atari_ST_GEM_Programming_1986/GEM_0904.pdf
*/

static const size_t VIDEO_RAM = 32000; // In bytes

static const uint8_t ST_LOW_DEPTH = 4;
static const size_t ST_LOW_HEIGHT = 200;
static const size_t ST_LOW_WIDTH = VIDEO_RAM * (8/ST_LOW_DEPTH) / ST_LOW_HEIGHT;

static const uint8_t ST_MEDIUM_DEPTH = 2;
static const size_t ST_MEDIUM_HEIGHT = 200;
static const size_t ST_MEDIUM_WIDTH = VIDEO_RAM * (8/ST_MEDIUM_DEPTH) / ST_MEDIUM_HEIGHT;

static const uint8_t ST_HIGH_DEPTH = 1;
static const size_t ST_HIGH_HEIGHT = 400;
static const size_t ST_HIGH_WIDTH = VIDEO_RAM * (8/ST_HIGH_DEPTH) / ST_HIGH_HEIGHT;

const char * atari_st_res_str(const enum atari_st_res res) {
	switch (res) {
	case atari_st_res_low: return "Low";
	case atari_st_res_medium: return "Medium";
	case atari_st_res_high: return "High";
	}
	return "???";
}

/* Common functions */

static void st_interleave(struct wuimg *img, const uint16_t *src,
const enum atari_st_res res) {
	const uint8_t planes = (res == atari_st_res_low)
		? ST_LOW_DEPTH : ST_MEDIUM_DEPTH;
	const size_t width = (res == atari_st_res_low)
		? ST_LOW_WIDTH : ST_MEDIUM_WIDTH;
	const size_t height = 200;

	const size_t src_stride = width/(16/planes);
	for (size_t y = 0; y < height; ++y) {
		for (size_t group = 0; group < width/16; ++group) {
			const size_t d_base = y*width + group*16;
			const size_t s_base = y*src_stride + (group * planes);
			for (uint8_t p = 0; p < planes; ++p) {
				const size_t s = endian16(src[s_base + p], big_endian);
				for (uint8_t bit = 0; bit < 16; ++bit) {
					img->data[d_base + bit] |= (uint8_t)(
						((s << bit) & 0x8000) >> (15 - p)
					);
				}
			}
		}
	}
}

static size_t load_raw(struct wuimg *img, const enum atari_st_res res, FILE *ifp) {
	if (res == atari_st_res_high) {
		return fread(img->data, 1, VIDEO_RAM, ifp);
	}
	size_t w = 0;
	uint16_t *ram = malloc(VIDEO_RAM);
	if (ram) {
		w = fread(ram, 1, VIDEO_RAM, ifp);
		st_interleave(img, ram, res);
		free(ram);
	}
	return w;
}

static enum wu_error set_pal(struct wuimg *img, const uint8_t src[static 32]) {
	struct palette *pal = wuimg_palette_init(img);
	if (pal) {
		const uint8_t sh = 8;
		const int scale = (0xff << sh) / 0x07 + 1;
		for (size_t i = 0; i < 16; ++i) {
			uint8_t rgb[3] = {
				src[i*2],
				src[i*2 + 1] >> 4,
				src[i*2 + 1],
			};
			for (size_t n = 0; n < sizeof(rgb); ++n) {
				rgb[n] = (uint8_t)(((rgb[n] & 7) * scale)
					>> sh);
			}
			pal->color[i] = (struct pix_rgba8) {
				.r = rgb[0],
				.g = rgb[1],
				.b = rgb[2],
				.a = 0xff,
			};
		}
		return wuimg_verify(img);
	}
	return wu_alloc_error;
}

static enum wu_error set_dims(struct wuimg *img, const enum atari_st_res res,
const void *restrict pal) {
	img->channels = 1;
	switch (res) {
	case atari_st_res_low:
		img->w = ST_LOW_WIDTH;
		img->h = ST_LOW_HEIGHT;
		img->bitdepth = 8;
		return set_pal(img, pal);
	case atari_st_res_medium:
		img->w = ST_MEDIUM_WIDTH;
		img->h = ST_MEDIUM_HEIGHT;
		img->bitdepth = 8;
		return set_pal(img, pal);
	case atari_st_res_high:
		img->w = ST_HIGH_WIDTH;
		img->h = ST_HIGH_HEIGHT;
		img->bitdepth = 1;
		img->attr = (buf_endian16(pal, big_endian) & 1)
			? pix_inverted : pix_normal;
		return wuimg_verify(img);
	}
	return wu_invalid_header;
}

/* Dali */

size_t dali_decode(struct dali_desc *desc, struct wuimg *img) {
	if (wuimg_alloc_noverify(img)) {
		return load_raw(img, desc->res, desc->ifp);
	}
	return 0;
}

static bool dali_ext(struct dali_desc *desc, const uint8_t ext[static 3]) {
	const char pre[] = {'s', 'd'};
	if (!memcmp(ext, pre, sizeof(pre))) {
		switch (ext[2]) {
		case '0': case '1': case '2':
			desc->res = ext[2] - '0';
			return true;
		}
	}
	return false;
}

enum wu_error dali_parse(struct dali_desc *desc, struct wuimg *img, FILE *ifp,
const uint8_t ext[static 3]) {
	/* Dali format:
		Offset  Type    Name
		0       u32     ID                 // Always 0
		4       u16     Palette[16]
		36      u8      Reserved[96]       // Often 0
		128     u16     ScreenDump[16000]
		32128
	*/

	*desc = (struct dali_desc) {
		.ifp = ifp,
	};
	if (dali_ext(desc, ext)) {
		uint32_t header[32];
		if (fread(header, sizeof(header), 1, ifp)) {
			if (!header[0]) {
				return set_dims(img, desc->res, header+1);
			}
			return wu_invalid_signature;
		}
		return wu_unexpected_eof;
	}
	return wu_unknown_file_type;
}

/* DEGAS */

void degas_free(struct degas_desc *desc) {
	free(desc->cycle);
}

static void load_elite_crng(struct degas_desc *desc, struct wuimg *img) {
	/* DEGAS Elite adds the following footer to normal DEGAS files:
		Offset  Type    Name
		0       u16     ColorAnimStart[4]
		8       u16     ColorAnimEnd[4]
		16      u16     AnimDirection[4]
		24      u16     AnimDelay[4]
		32
	 * One may even find High Depth files like this, for whatever reason.
	*/

	enum elite_direction {
		elite_left = 0, // high to low i guess??
		elite_off = 1,
		elite_right = 2,
	};

	const uint16_t slots = 4;
	struct palette_cycle *cycle = palette_cycle_new(slots);
	if (cycle) {
		desc->cycle = cycle;
		uint16_t buf[4][4];
		if (fread(buf, sizeof(buf), 1, desc->ifp)) {
			const unsigned max_idx = 16;
			for (size_t i = 0; i < slots; ++i) {
				uint16_t lo = endian16(buf[0][i], big_endian);
				uint16_t hi = endian16(buf[1][i], big_endian);
				enum elite_direction direction = endian16(
					buf[2][i], big_endian);
				uint16_t delay = endian16(buf[3][i], big_endian);
				cycle->crng[i] = (struct palette_crng) {
					.lo = (uint8_t)lo,
					.hi = (uint8_t)hi,
					.reverse = direction == elite_left,
					.active = !(direction & 1) && lo < hi
						&& hi < max_idx && delay < 128,
					.secs = (128 - delay) / 60.f,
				};
				cycle->active_nr += cycle->crng[i].active;
			}
			if (cycle->active_nr) {
				cycle->len = slots;
				palette_cycle_set(cycle, img->u.palette);
				img->evolving = true;
			}
		}
	}
}

static size_t degas_decomp(struct degas_desc *desc, struct wuimg *img) {
	int8_t *pb = malloc(desc->size);
	if (pb) {
		const size_t pb_len = fread(pb, 1, desc->size, desc->ifp);
		uint8_t *unpack;
		if (desc->res == atari_st_res_high) {
			unpack = img->data;
		} else {
			unpack = malloc(VIDEO_RAM);
			if (!unpack) {
				free(pb);
				return 0;
			}
		}
		const size_t w = decomp_pack_bits(unpack, VIDEO_RAM, pb, pb_len);
		free(pb);
		if (unpack != img->data) {
			const uint8_t planes = (desc->res == atari_st_res_low)
				? ST_LOW_DEPTH : ST_MEDIUM_DEPTH;

			const size_t outstride = wuimg_stride(img);
			const size_t instride = strip_length(img->w, 1, 1)
				* planes;
			for (size_t y = 0; y < 200; ++y) {
				bitplane_interleave_row8(img->data + outstride*y,
					unpack + instride*y, img->w, planes, 1);
			}
			free(unpack);
			load_elite_crng(desc, img);
		}
		return w;
	}
	return 0;
}

size_t degas_decode(struct degas_desc *desc, struct wuimg *img) {
	if (wuimg_alloc_noverify(img)) {
		if (desc->compressed) {
			return degas_decomp(desc, img);
		}
		const size_t w = load_raw(img, desc->res, desc->ifp);
		if (desc->res != atari_st_res_high && desc->is_elite) {
			load_elite_crng(desc, img);
		}
		return w;
	}
	return 0;
}

enum wu_error degas_parse(struct degas_desc *desc, struct wuimg *img,
FILE *ifp) {
	/* DEGAS format:
		Offset  Type    Name
		0       u16     Flags
		2       u16     Palette[16]
		34      u16     Data[]
	 * For uncompressed files, Data is 16000 words in size, and so the file
	 *   should be 32034 bytes. If it's 32066 bytes (32034 + 32) instead,
	 *   then it's a DEGAS Elite file, and has a color animation struct
	 *   at the end.
	 * For compressed files, the color animation struct is always present,
	 *   and so the size of Data is the file size minus header and footer.
	*/

	desc->cycle = NULL;
	uint16_t src[17];
	if (fread(src, sizeof(src), 1, ifp)) {
		const uint16_t flags = endian16(src[0], big_endian);
		const bool compressed = flags & 0x8000;
		const enum atari_st_res res = flags & ~0x8000;
		const size_t size_limit = compressed
			? VIDEO_RAM*2 : VIDEO_RAM;
		size_t rem = file_remaining(ifp);
		if (compressed) {
			const size_t crng_size = 4*4*2;
			if (rem <= crng_size) {
				return wu_unexpected_eof;
			}
			rem -= crng_size;
		}
		*desc = (struct degas_desc) {
			.ifp = ifp,
			.res = res,
			.compressed = compressed,
			.is_elite = compressed || rem + 34 >= 32066,
			.size = zumin(rem, size_limit),
		};
		return set_dims(img, desc->res, src + 1);
	}
	return wu_unexpected_eof;
}

/* MegaPaint */

static size_t bld_rle(uint8_t *restrict dst, const size_t dst_len,
const uint8_t *restrict src, const size_t src_len) {
	size_t d = 0;
	size_t s = 0;
	while (s < src_len) {
		const uint8_t val = src[s];
		++s;
		size_t run = 1;
		switch (val) {
		case 0x00: case 0xff:
			if (s >= src_len) {
				return d;
			}
			run = src[s] + 1;
			++s;
			break;
		}
		if (d + run > dst_len) {
			break;
		}
		memset(dst + d, val, run);
		d += run;
	}
	return d;
}

size_t bld_decode(struct bld_desc *desc, struct wuimg *img) {
	size_t w = 0;
	if (wuimg_alloc_noverify(img)) {
		if (desc->compressed) {
			size_t rem = file_remaining(desc->ifp);
			uint8_t *rle = malloc(rem);
			if (rle) {
				const size_t len = fread(rle, 1, rem, desc->ifp);
				w = bld_rle(img->data, wuimg_size(img),
					rle, len);
				free(rle);
			}
		} else {
			w = fmt_load_raster(img, desc->ifp);
		}
	}
	return w;
}

enum wu_error bld_parse(struct bld_desc *desc, struct wuimg *img, FILE *ifp) {
	/* MegaPaint format:
		Offset  Type    Name
		0       s16     Width  // If width negative, file is compressed
		2       s16     Height // Actual dims are the absolute value + 1
		4
	 * MegaPaint extension and header conflict with BSAVE formats.
	*/
	uint16_t buf[2];
	if (fread(buf, sizeof(buf), 1, ifp)) {
		int16_t height = (int16_t)endian16(buf[1], big_endian);
		if (height > 0) {
			int16_t width = (int16_t)endian16(buf[0], big_endian);
			*desc = (struct bld_desc) {
				.ifp = ifp,
				.compressed = width < 0,
			};
			img->w = (size_t)abs(width) + 1;
			img->h = (size_t)height + 1;
			img->channels = 1;
			img->bitdepth = 1;
			img->attr = pix_inverted;
			return wuimg_verify(img);
		}
		return wu_invalid_header;
	}
	return wu_unexpected_eof;
}

/* Tiny Stuff */

void tiny_free(struct tiny_desc *desc) {
	free(desc->cycle);
}

static void tiny_unscramble(uint16_t *restrict dst, const uint16_t *restrict src) {
	const unsigned y_stride = 80;
	const unsigned set_stride = 200*20;
	for (uint8_t y = 0; y < 200; ++y) {
		for (uint8_t x = 0; x < 20; ++x) {
			for (uint8_t set = 0; set < 4; ++set) {
				dst[y*y_stride + x*4 + set] =
					src[set_stride*set + x*200 + y];
			}
		}
	}
}

static size_t tiny_rle(uint16_t *out, const int8_t *ctrl, const size_t clen,
const struct wuptr data) {
	// Not quite same as ILBM VDAT
	size_t dpos = 0, cpos = 0, opos = 0;
	while (cpos < clen) {
		const int8_t c = ctrl[cpos];
		++cpos;
		size_t run;
		bool cpy;
		if (c < 0) {
			run = (size_t)-c;
			cpy = true;
		} else if (c > 1) {
			run = (size_t)c;
			cpy = false;
		} else {
			if (cpos + 2 > clen) {
				break;
			}
			run = buf_endian16(ctrl + cpos, big_endian);
			cpos += 2;
			cpy = (bool)c;
		}

		if (opos + run > VIDEO_RAM/2) {
			break;
		}
		if (cpy) {
			if (dpos + run*2 > data.len) {
				break;
			}
			memcpy(out + opos, data.ptr + dpos, run*2);
			dpos += run*2;
		} else {
			if (dpos + 2 > data.len) {
				break;
			}
			memset16(out + opos, data.ptr + dpos, run);
			dpos += 2;
		}
		opos += run;
	}
	return opos;
}

size_t tiny_decode(const struct tiny_desc *desc, struct wuimg *img) {
	size_t w = 0;
	if (wuimg_alloc_noverify(img)) {
		struct mparser mp = desc->mp;
		const void *ctrl = mp_next_slice(&mp, desc->ctrl);
		struct wuptr data = mp_next_remaining(&mp, desc->data*2);
		if (ctrl) {
			const bool high_res = desc->res == atari_st_res_high;
			uint16_t *buf = malloc(VIDEO_RAM * (high_res ? 1 : 2));
			if (buf) {
				w = tiny_rle(buf, ctrl, desc->ctrl, data);
				void *un = high_res
					? img->data : (uint8_t *)buf + VIDEO_RAM;
				tiny_unscramble(un, buf);
				if (!high_res) {
					st_interleave(img, un, desc->res);
				}
				free(buf);
			}
		}
	}
	return w;
}

enum wu_error tiny_parse(struct tiny_desc *desc, struct wuimg *img,
const struct map_info map) {
	/* Tiny header:
		Offset  Type    Name
		0       u8      Resolution
		1

	 * If Resolution >= 3:
		1       u8      CycleRange
		2       s8      CycleSpeed
		3       u16     Iterations
		5

		+0      u16     Palette[16]
		+32     u16     ControlLen
		+34     u16     DataLen
		+36     s8      ControlBytes[ControlLen]
		...     u16     DataWords[DataLen]
	*/
	*desc = (struct tiny_desc) {
		.mp = mp_map(map),
	};

	const uint8_t *header = mp_next_slice(&desc->mp, 1);
	if (!header) {
		return wu_unexpected_eof;
	}

	const uint8_t *crng = NULL;
	uint8_t res = header[0];
	if (res >= 3) {
		res -= 3;
		crng = mp_next_slice(&desc->mp, 4);
	}
	header = mp_next_slice(&desc->mp, 36);
	if (!header) {
		return wu_unexpected_eof;
	}

	desc->ctrl = buf_endian16(header + 32, big_endian);
	desc->data = buf_endian16(header + 34, big_endian);
	desc->res = res;
	enum wu_error st = set_dims(img, res, header);
	if (crng && img->mode == image_mode_palette && st == wu_ok) {
		struct palette_cycle *cycle = palette_cycle_new(1);
		if (!cycle) {
			return wu_alloc_error;
		}
		desc->cycle = cycle;
		desc->iters = buf_endian16(crng + 2, big_endian);
		palette_cycle_set(cycle, img->u.palette);
		const int8_t speed = (int8_t)crng[1];
		cycle->crng[0] = (struct palette_crng) {
			.lo = crng[0] >> 4,
			.hi = crng[0] & 0xf,
			.reverse = speed < 0,
			.active = speed,
			.secs = (float)abs(speed)/60.f,
		};
		cycle->len = 1;
		cycle->active_nr = (bool)speed;
		img->evolving = speed;
	}
	return st;
}

// SPDX-License-Identifier: 0BSD
#include "misc/common.h"
#include "misc/decomp.h"
#include "misc/endian.h"
#include "misc/math.h"
#include "raster/graphics_adapters.h"

#include "lib/degas.h"

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

const size_t VIDEO_RAM = 32000;

const uint8_t ST_LOW_DEPTH = 4;
const size_t ST_LOW_HEIGHT = 200;
const size_t ST_LOW_WIDTH = VIDEO_RAM * (8/ST_LOW_DEPTH) / ST_LOW_HEIGHT;

const uint8_t ST_MEDIUM_DEPTH = 2;
const size_t ST_MEDIUM_HEIGHT = 200;
const size_t ST_MEDIUM_WIDTH = VIDEO_RAM * (8/ST_MEDIUM_DEPTH) / ST_MEDIUM_HEIGHT;

const uint8_t ST_HIGH_DEPTH = 1;
const size_t ST_HIGH_HEIGHT = 400;
const size_t ST_HIGH_WIDTH = VIDEO_RAM * (8/ST_HIGH_DEPTH) / ST_HIGH_HEIGHT;

const char * degas_res_str(const enum degas_res res) {
	switch (res) {
	case degas_res_low: return "Low";
	case degas_res_medium: return "Medium";
	case degas_res_high: return "High";
	}
	return "???";
}

static void st_interleave(struct wuimg *img, const uint16_t *src,
const enum degas_res res) {
	const uint8_t planes = (res == degas_res_low)
		? ST_LOW_DEPTH : ST_MEDIUM_DEPTH;
	const size_t width = (res == degas_res_low)
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

static size_t st_decomp(const struct degas_desc *desc, struct wuimg *img) {
	int8_t *pb = malloc(desc->size);
	if (pb) {
		const size_t pb_len = fread(pb, 1, desc->size, desc->ifp);
		uint8_t *unpack;
		if (desc->res == degas_res_high) {
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
			const uint8_t planes = (desc->res == degas_res_low)
				? ST_LOW_DEPTH : ST_MEDIUM_DEPTH;

			const size_t outstride = wuimg_stride(img);
			const size_t instride = strip_length(img->w, 1, 1)
				* planes;
			for (size_t y = 0; y < 200; ++y) {
				bitplane_interleave_row8(img->data + outstride*y,
					unpack + instride*y, img->w, planes, 1);
			}
			free(unpack);
		}
		return w;
	}
	return 0;
}

size_t degas_decode(const struct degas_desc *desc, struct wuimg *img) {
	if (wuimg_alloc_noverify(img)) {
		if (desc->compressed) {
			return st_decomp(desc, img);
		}
		if (desc->res == degas_res_high) {
			return fread(img->data, 1, VIDEO_RAM, desc->ifp);
		}
		uint16_t *ram = malloc(VIDEO_RAM);
		if (ram) {
			const size_t w = fread(ram, 1, VIDEO_RAM, desc->ifp);
			st_interleave(img, ram, desc->res);
			free(ram);
			return w;
		}
	}
	return 0;
}

enum wu_error degas_parse(struct degas_desc *desc, struct wuimg *img) {
	/* DEGAS header (after Flags):
		Offset  Type    Name
		0       u16     Palette[16]
		32
	*/

	uint8_t src[32];
	if (!fread(src, sizeof(src), 1, desc->ifp)) {
		return wu_unexpected_eof;
	}

	const size_t size_limit = desc->compressed
		? VIDEO_RAM*2 : VIDEO_RAM;
	desc->size = zumin(file_remaining(desc->ifp), size_limit);

	img->channels = 1;
	switch (desc->res) {
	case degas_res_low:
		img->w = ST_LOW_WIDTH;
		img->h = ST_LOW_HEIGHT;
		img->bitdepth = 8;
		break;
	case degas_res_medium:
		img->w = ST_MEDIUM_WIDTH;
		img->h = ST_MEDIUM_HEIGHT;
		img->bitdepth = 8;
		break;
	case degas_res_high:
		img->w = ST_HIGH_WIDTH;
		img->h = ST_HIGH_HEIGHT;
		img->bitdepth = 1;
		img->attr = (buf_endian16(src, big_endian) & 1)
			? pix_inverted : pix_normal;
		return wuimg_verify(img);
	}

	struct raster_pal *pal = wuimg_palette_init(img);
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

enum wu_error degas_open(struct degas_desc *desc, FILE *ifp) {
	/* DEGAS header:
		Offset  Type    Name
		0       u16     Flags
		2
	*/
	desc->ifp = ifp;
	uint16_t flags;
	if (fread(&flags, sizeof(flags), 1, ifp)) {
		flags = endian16(flags, big_endian);
		const enum degas_res res = flags & 3;
		switch (res) {
		case degas_res_low:
		case degas_res_medium:
		case degas_res_high:
			desc->res = res;
			desc->compressed = flags & 0x8000;
			return wu_ok;
		}
		return wu_unknown_file_type;
	}
	return wu_unexpected_eof;
}

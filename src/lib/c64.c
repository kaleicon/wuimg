// SPDX-License-Identifier: 0BSD
#include "misc/common.h"
#include "misc/math.h"
#include "misc/mem.h"

#include "c64.h"

/* C64 files usually contain an uncompressed dump of video memory. Each format
 * stores it a bit different. A good reference is
https://codebase64.org/doku.php?id=base:c64_grafix_files_specs_list_v0.03
 * Subtract the Load Address and add 2 to get the actual memory positions in
 * the file.

 * In Hires mode, there are two memory sections: Bitmap and Screen.
 * Bitmap is a series of tiles that are 8 bytes in size, corresponding to a
 * 8x8 pixel area. Each byte is a 8 pixel row, and each bit a pixel.
 * Upper bits are leftmost, and tiles go left to right, top to bottom.
 * To render, keep track of the tile number, read bit from a tile byte,
 * and select a nibble from Screen according to its value:
 *   0: Use Screen[TileN] lower nibble
 *   1: Use Screen[TileN] upper nibble
 * These are palette indices. Thus, each 8x8 region is limited to two colors.

 * Multicolor mode has four sections to make use of four colors per tile:
 * Bitmap, Screen, Color, and a single Background byte. Tiles are still
 * 8 bytes, but now use bit pairs to select the color source, and so they span
 * a 4x8 pixel area. The image must be stretched afterwards (its width doubled)
 * to match the size of Hires mode.
 * To render, keep track of the tile number, read 2 bits from a tile byte, and
 * read a nibble according to their value:
 *   0: Use Background lower nibble
 *   1: Use Screen[TileN] upper nibble
 *   2: Use Screen[TileN] lower nibble
 *   3: Use Color[TileN] lower nibble
 * Even though only the lower nibble of Background and Color is used, they
 * may contain junk on the upper bits, so they must be masked regardless.
*/

static const size_t TW = 40; // Width in tiles, both modes
static const size_t TH = 25; // Height in tiles

static const size_t HR_WIDTH = TW*8;
static const size_t MC_WIDTH = TW*4;
static const size_t HEIGHT = TH*8;

static const size_t RAM_LEN = TW * TH;
static const size_t BITMAP_LEN = RAM_LEN * 8;
static const size_t BG_LEN = 1;

struct c64_mem_offsets {
	const uint8_t *restrict bitmap;
	const uint8_t *restrict screen;
	const uint8_t *restrict color;
	const uint8_t *restrict bg;
};

const char * c64_mode_str(const enum c64_mode mode) {
	switch (mode) {
	case c64_hires: return "Hires";
	case c64_multicolor: return "Multicolor";
	}
	return "???";
}

const char * c64_fmt_str(const enum c64_fmt fmt) {
	switch (fmt) {
	case c64_art_studio: return "Art Studio";
	case c64_advanced_art_studio: return "Advanced Art Studio";
	case c64_artist64: return "Wigmore Artist64";
	case c64_blazing_paddles: return "Blazing Paddles";
	case c64_doodle: return "Doodle";
	case c64_hi_eddi: return "Hi-Eddi";
	case c64_image_system_m: return "Image System";
	case c64_koalapainter: return "KoalaPainter";
	case c64_saracen_paint: return "Saracen Paint";
	case c64_vidcom_64: return "Vidcom 64";
	}
	return "???";
}

static void multicolor_expand(uint16_t *dst, const struct c64_mem_offsets *off) {
	const uint8_t bg = *off->bg & 0x0f;
	for (size_t tile_y = 0; tile_y < TH; ++tile_y) {
		for (size_t tile_x = 0; tile_x < TW; ++tile_x) {
			const size_t tile = tile_y*TW + tile_x;

			// Grab all colors sources unconditionally
			const uint8_t st = off->screen[tile] >> 4,
				sb = off->screen[tile] & 0x0f,
				c = off->color[tile];

			/* OR them into a word, arranged such that we can
			 * retrieve them using a bit couple as shr argument. */
			const uint16_t src = c << 12 | sb << 8 | st << 4 | bg;
			for (size_t y = 0; y < 8; ++y) {
				const uint8_t byte = off->bitmap[tile*8 + y];
				uint16_t out = 0;
				for (size_t x = 0; x < 4; ++x) {
					uint8_t couple = (byte >> (x*2)) & 3;
					uint8_t n = (src >> (couple * 4)) & 0xf;
					out |= n << (x*4);
				}
				const size_t d = (tile_y*8 + y)*TW + tile_x;
				dst[d] = endian16(out, big_endian);
			}
		}
	}
}

static void hires_expand(uint32_t *dst, const struct c64_mem_offsets *off) {
	for (size_t tile_y = 0; tile_y < TH; ++tile_y) {
		for (size_t tile_x = 0; tile_x < TW; ++tile_x) {
			const size_t tile = tile_y*TW + tile_x;
			for (size_t y = 0; y < 8; ++y) {
				uint8_t byte = off->bitmap[tile*8 + y];
				uint8_t src = off->screen[tile];
				uint32_t out = 0;
				for (size_t x = 0; x < 8; ++x) {
					uint8_t b = (byte >> x) & 1;
					uint8_t n = (src >> (b*4)) & 0xf;
					out |= n << (x*4);
				}
				const size_t d = (tile_y*8 + y)*TW + tile_x;
				dst[d] = endian32(out, big_endian);
			}
		}
	}
}

static bool get_offsets(struct mparser *mp, struct c64_mem_offsets *off,
const enum c64_fmt fmt) {
	mp_seek_cur(mp, 2);
	switch (fmt) {
	case c64_art_studio:
		off->bitmap = mp_slice(mp, BITMAP_LEN);
		off->screen = mp_slice(mp, RAM_LEN);
		return off->screen;
	case c64_doodle:
		off->screen = mp_slice(mp, RAM_LEN);
		mp_seek_cur(mp, 0x18);
		off->bitmap = mp_slice(mp, BITMAP_LEN);
		return off->bitmap;
	case c64_hi_eddi:
		off->bitmap = mp_slice(mp, BITMAP_LEN);
		mp_seek_cur(mp, 0xc0);
		off->screen = mp_slice(mp, RAM_LEN);
		return off->screen;

	case c64_advanced_art_studio:
		off->bitmap = mp_slice(mp, BITMAP_LEN);
		off->screen = mp_slice(mp, RAM_LEN);
		mp_seek_cur(mp, 1); // border
		off->bg = mp_slice(mp, BG_LEN);
		mp_seek_cur(mp, 0xe);
		off->color = mp_slice(mp, RAM_LEN);
		return off->color;
	case c64_artist64:
		off->bitmap = mp_slice(mp, BITMAP_LEN);
		mp_seek_cur(mp, 0xc0);
		off->screen = mp_slice(mp, RAM_LEN);
		mp_seek_cur(mp, 0x18);
		off->color = mp_slice(mp, RAM_LEN);
		mp_seek_cur(mp, 0x17);
		off->bg = mp_slice(mp, BG_LEN);
		return off->bg;
	case c64_blazing_paddles:
		off->bitmap = mp_slice(mp, BITMAP_LEN);
		mp_seek_cur(mp, 0x40);
		off->bg = mp_slice(mp, BG_LEN);
		mp_seek_cur(mp, 0x7f);
		off->screen = mp_slice(mp, RAM_LEN);
		mp_seek_cur(mp, 0x18);
		off->color = mp_slice(mp, RAM_LEN);
		return off->color;
	case c64_image_system_m:
		off->color = mp_slice(mp, RAM_LEN);
		mp_seek_cur(mp, 0x18);
		off->bitmap = mp_slice(mp, BITMAP_LEN);
		mp_seek_cur(mp, 0xbf);
		off->bg = mp_slice(mp, BG_LEN);
		off->screen = mp_slice(mp, RAM_LEN);
		return off->screen;
	case c64_koalapainter:
		off->bitmap = mp_slice(mp, BITMAP_LEN);
		off->screen = mp_slice(mp, RAM_LEN);
		off->color = mp_slice(mp, RAM_LEN);
		off->bg = mp_slice(mp, BG_LEN);
		return off->bg;
	case c64_saracen_paint:
		off->screen = mp_slice(mp, RAM_LEN);
		mp_seek_cur(mp, 8);
		off->bg = mp_slice(mp, BG_LEN);
		mp_seek_cur(mp, 0xf);
		off->bitmap = mp_slice(mp, BITMAP_LEN);
		mp_seek_cur(mp, 0xc0);
		off->color = mp_slice(mp, RAM_LEN);
		return off->color;
	case c64_vidcom_64:
		off->color = mp_slice(mp, RAM_LEN);
		mp_seek_cur(mp, 0x18);
		off->screen = mp_slice(mp, RAM_LEN);
		off->bg = mp_slice(mp, BG_LEN);
		mp_seek_cur(mp, 0x17);
		off->bitmap = mp_slice(mp, BITMAP_LEN);
		return off->bitmap;
	}
	return false;
}

static bool ggjj_decode(uint8_t *restrict dst, const size_t dst_len,
struct mparser *rle) {
	const uint8_t RLE_FLAG = 0xfe;
	size_t d = 0;
	size_t r = 0;
	while (d < dst_len) {
		const size_t w = memccpy_cur(dst + d, rle->mem + r, RLE_FLAG,
			dst_len - d, rle->len - r);
		d += w;
		r += w;
		if (r + 3 > rle->len) {
			if (dst_len - d == 1) {
				// Fix for a file that omits Background byte
				dst[d] = 0;
				++d;
			}
			break;
		}

		uint8_t val = rle->mem[r+1];
		size_t count = rle->mem[r+2];
		r += 3;
		if (d + count > dst_len) {
			// The stream goes on, but we've got all we need
			count = dst_len - d;
		}
		memset(dst + d, val, count);
		d += count;
	}
	*rle = mp_mem(d, dst);
	return d == dst_len;
}

static unsigned ggjj_needed(const enum c64_fmt fmt) {
	return fmt == c64_doodle ? 9026 : 10003;
}

bool c64_decode(const struct c64_desc *desc, struct wuimg *img) {
	bool ok = false;
	if (wuimg_alloc_noverify(img)) {
		struct mparser mp = desc->mp;
		struct c64_mem_offsets off = {0};
		uint8_t *uncomp = NULL;
		if (desc->compressed) {
			const unsigned upack_len = ggjj_needed(desc->fmt);
			uncomp = malloc(upack_len);
			if (uncomp) {
				ok = ggjj_decode(uncomp, upack_len, &mp);
			}
		} else {
			ok = true;
		}

		if (ok) {
			ok = get_offsets(&mp, &off, desc->fmt);
			if (ok) {
				if (desc->mode == c64_hires) {
					hires_expand((uint32_t *)img->data, &off);
				} else {
					multicolor_expand((uint16_t *)img->data, &off);
				}
			}
		}
		free(uncomp);
	}
	return ok;
}

inline static struct pix_rgb8 gen_e(const uint8_t level, const uint8_t angle) {
	/* Generate the 'colodore' YUV palette.
	   https://www.pepto.de/projects/colorvic/
	*/

	const float sector = 360.f/16;
	const float origin = sector/2;
	const float radian = (float)(M_PI/180);
	const float screen = 1.f/5;

	const float pscreen = 1 + screen;
	const float saturation = 50.f * (1 - screen);

	float u = 128;
	float v = 128;
	if (angle) {
		const float r = fmaf(angle, sector, origin) * radian;
		const float psat = saturation * pscreen;
		u = fmaf(cosf(r), psat, u);
		v = fmaf(sinf(r), psat, v);
	}
	const float y = level * 8 * pscreen;
	return (struct pix_rgb8) {
		(uint8_t)fminf(y, 255),
		(uint8_t)(fclampf(u, 0, 255)),
		(uint8_t)(fclampf(v, 0, 255)),
	};
}

enum wu_error c64_set(const struct c64_desc *desc, struct wuimg *img) {
	img->w = desc->mode == c64_hires ? HR_WIDTH : MC_WIDTH;
	img->h = HEIGHT;
	img->channels = 1;
	img->bitdepth = 4;
	img->ratio = desc->mode == c64_hires ? 1 : 2;
	img->cs.primaries = cicp_primaries_bt470_6_system_b_g;
	img->cs.transfer = cicp_transfer_bt470_6_system_b_g;
	img->cs.matrix = cicp_matrix_bt470_6_system_b_g;
	struct palette *pal = wuimg_palette_init(img);
	if (pal) {
		const struct pix_rgb8 c64_pal[16] = {
			gen_e(0, 0),
			gen_e(32, 0),
			gen_e(10, 4),
			gen_e(20, 12),
			gen_e(12, 2),
			gen_e(16, 10),
			gen_e(8, 15),
			gen_e(24, 7),
			gen_e(12, 5),
			gen_e(8, 6),
			gen_e(16, 4),
			gen_e(10, 0),
			gen_e(15, 0),
			gen_e(24, 10),
			gen_e(15, 15),
			gen_e(20, 0),
		};
		palette_from_rgb8(pal, c64_pal, ARRAY_LEN(c64_pal));
		return wuimg_verify(img);
	}
	return wu_alloc_error;
}

enum wu_error c64_guess(struct c64_desc *desc, const struct wuptr mem,
const uint8_t ext[static 4]) {
	*desc = (struct c64_desc) {
		.mp = mp_wuptr(mem),
	};
	enum c64_fmt f;
	enum c64_mode m = c64_multicolor;
	const char *e = (const char *)ext;
	bool gg = !strcmp(e, "gg");
	if (gg || !strcmp(e, "jj")) {
		// Koala or Doodle compressed file
		desc->compressed = true;
		f = gg ? c64_koalapainter : c64_doodle;
		m = gg ? c64_multicolor : c64_hires;
		// Ensure length is not outrageous
		//   e.g. 0xfe 0x01 0xfe  0xfe 0x01 0xfe ...
		const unsigned upack_len = ggjj_needed(f);
		if (mem.len >= upack_len * 3) {
			return wu_unknown_file_type;
		}
	} else {
		switch (mem.len) {
		case 9002:
		case 9003:
		case 9009: f = c64_art_studio; m = c64_hires; break;
		case 9026:
		case 9217:
		case 9346: f = c64_doodle; m = c64_hires; break;
		case 9194: f = c64_hi_eddi; m = c64_hires; break;
		case 9218:
			if (!strcmp(e, "hed")) {
				f = c64_hi_eddi;
			} else {
				f = c64_doodle;
			}
			m = c64_hires;
			break;

		case 10001:
		case 10003:
		case 10004:
		case 10006:
		case 10007: f = c64_koalapainter; break;
		case 10018: f = c64_advanced_art_studio; break;
		case 10050: f = c64_vidcom_64; break;
		case 10218: f = c64_image_system_m; break;
		case 10219:
			/* TODO: Some files are truncated to 10018 bytes.
			 * Padding with zeros makes them display fine. */
			f = c64_saracen_paint; break;
		case 10242:
			if (!strcmp(e, "a64") || !strcmp(e, "wig")) {
				f = c64_artist64;
			} else {
				f = c64_blazing_paddles;
			}
			break;
		default: return wu_unknown_file_type;
		}
	}
	desc->fmt = f;
	desc->mode = m;
	return wu_ok;
}

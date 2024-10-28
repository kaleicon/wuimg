// SPDX-License-Identifier: 0BSD
#include "misc/common.h"
#include "misc/math.h"
#include "misc/mem.h"

#include "c64.h"

static const size_t RAM_LEN = 0x3e8;
static const size_t BITMAP_LEN = RAM_LEN << 3;
static const size_t BG_LEN = 1;
static const size_t TOTAL_LEN = BITMAP_LEN + RAM_LEN*2 + BG_LEN + 2;

static const size_t WIDTH = 160;
static const size_t HEIGHT = 200;

enum c64_fmt_sizes {
	c64_koa = 10003,
	c64_ocp = 10018,
};

struct c64_mem_offsets {
	const uint8_t *restrict bitmap;
	const uint8_t *restrict screen;
	const uint8_t *restrict color;
	const uint8_t *restrict bg;
};

static void multicolor_expand(uint8_t *restrict dst,
const struct c64_mem_offsets *off) {
	const uint8_t bg = *off->bg & 0x0f;
	for (size_t tile_y = 0; tile_y < HEIGHT/8; ++tile_y) {
		const size_t tile_block = (tile_y * 8) * 5;
		for (size_t y = 0; y < 8; ++y) {
			for (size_t tile_x = 0; tile_x < WIDTH/4; ++tile_x) {
				const size_t tile = tile_block + tile_x;
				const uint8_t byte = off->bitmap[(tile << 3) + y];
				for (size_t x = 0; x < 4; ++x) {
					const uint8_t couplet = (byte >> (6 - x*2)) & 3;
					uint8_t b;
					switch (couplet) {
					case 0: b = bg; break;
					case 1: b = off->screen[tile] >> 4; break;
					case 2: b = off->screen[tile] & 0x0f; break;
					case 3: b = off->color[tile] & 0x0f; break;
					}
					const size_t pos = (tile_y*8 + y)*WIDTH
						+ tile_x*4 + x;
					dst[pos/2] |= b << ((x & 1) ? 0 : 4);
				}
			}
		}
	}
}

static bool contiguous_mem(struct mparser *mp, const size_t adv,
struct c64_mem_offsets *off) {
	mp_next_slice(mp, adv);
	off->bitmap = mp_next_slice(mp, BITMAP_LEN);
	off->screen = mp_next_slice(mp, RAM_LEN);
	off->color = mp_next_slice(mp, RAM_LEN);
	off->bg = mp_next_slice(mp, BG_LEN);
	return off->bg;
}

static bool gg_decode(uint8_t *restrict dst, const size_t dst_len,
const struct mparser rle, struct wuimg *img, struct c64_mem_offsets *off) {
	const uint8_t RLE_FLAG = 0xfe;
	size_t d = 0;
	size_t r = 0;
	for (;;) {
		const size_t w = memccpy_cur(dst + d, rle.mem + r, RLE_FLAG,
			dst_len - d, rle.len - r);
		d += w;
		r += w;
		if (r + 2 >= rle.len) {
			break;
		}
		const uint8_t val = rle.mem[r+1];
		const uint8_t count = rle.mem[r+2];
		r += 3;
		if (d + count >= dst_len) {
			break;
		}
		memset(dst + d, val, count);
		d += count;
	}
	if (d == dst_len) {
		struct mparser mp = mp_mem(dst_len, dst);
		if (contiguous_mem(&mp, 2, off)) {
			multicolor_expand(img->data, off);
			return true;
		}
	}
	return false;
}

bool c64_decode(const struct mparser *mp_orig, struct wuimg *img) {
	bool ok = false;
	if (wuimg_alloc_noverify(img)) {
		struct mparser mp = *mp_orig;
		struct c64_mem_offsets off;
		switch (mp.len) {
		case c64_koa:
			if (contiguous_mem(&mp, 2, &off)) {
				multicolor_expand(img->data, &off);
				ok = true;
			}
			break;
		case c64_ocp:
			mp_next_slice(&mp, 2);
			off.bitmap = mp_next_slice(&mp, BITMAP_LEN);
			off.screen = mp_next_slice(&mp, RAM_LEN);
			mp_next_slice(&mp, 1); // border
			off.bg = mp_next_slice(&mp, BG_LEN);
			off.color = mp_next_slice(&mp, RAM_LEN);
			if (off.color) {
				multicolor_expand(img->data, &off);
				ok = true;
			}
			break;
		default:
			;uint8_t *uncomp = malloc(TOTAL_LEN);
			if (uncomp) {
				ok = gg_decode(uncomp, TOTAL_LEN, mp, img, &off);
				free(uncomp);
			}
		}
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

static enum wu_error multicolor_settings(struct wuimg *img) {
	img->w = WIDTH;
	img->h = HEIGHT;
	img->channels = 1;
	img->bitdepth = 4;
	img->ratio = 2;
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

enum wu_error c64_guess(const struct mparser *mp, struct wuimg *img) {
	bool ok = false;
	switch (mp->len) {
	case c64_koa:
	case c64_ocp:
		ok = true;
		break;
	default:
		// e.g. 0xfe 0x01 0xfe  0xfe 0x01 0xfe ...
		;const size_t pathological_rle = TOTAL_LEN*3;
		ok = (mp->len < pathological_rle);
	}
	return (ok) ? multicolor_settings(img) : wu_unknown_file_type;
}

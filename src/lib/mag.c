// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2022 kaleido
#include <stdlib.h>
#include <string.h>

#include "misc/bit.h"
#include "misc/file.h"
#include "misc/mem.h"
#include "raster/fmt.h"
#include "raster/graphics_adapters.h"
#include "mag.h"

/* Based on
https://mooncore.eu/bunny/txt/makichan.htm

 * and
https://mooncore.eu/bunny/txt/magerrata_e.txt

 * with some fixes to make the MSX Pixel Art Collection and telparia.com
 * samples work. (Some images NSFW).
https://frs.badcoffee.info/MSXart.html
https://telparia.com/fileFormatSamples/image/makichan/

 * MAKI01 is implemented in maki.c, as it has almost nothing in common with
 * MAG/MAKI02.
*/

const char * mag_screen_mode_str(const enum mag_screen_mode mode) {
	switch (mode) {
	case mag_screen_mode_pc98_standard: return "Standard PC98";
	case mag_screen_mode_msx_sc7: return "MSX SC7";
	case mag_screen_mode_vm98: return "VM98";
	case mag_screen_mode_pc88_analog_pal: return "PC88 wih analog palette";
	case mag_screen_mode_pc98_old: return "Old PC98";
	case mag_screen_mode_pc88_standard: return "Standard PC88";
	case mag_screen_mode_msx_sc8: return "MSX SC8";
	}
	return "???";
}

const char * mag_msx_screen_str(const enum mag_msx_screen flag) {
	switch (flag) {
	case mag_msx2_screen7: return "MSX2 SCREEN 7";
	case mag_msx2_screen8: return "MSX2 SCREEN 8";
	case mag_msx2p_screen10: return "MSX2+ SCREEN 10";
	case mag_msx2p_screen11: return "MSX2+ SCREEN 11";
	case mag_msx2p_screen12: return "MSX2+ SCREEN 12";
	case mag_msx2_screen5: return "MSX2 SCREEN 5";
	case mag_msx2_screen6: return "MSX2 SCREEN 6";
	}
	return "???";
}

const char * mag_model_code_str(const enum mag_model_code code) {
	switch (code) {
	case mag_model_msx: return "MSX";
	case mag_model_98sa: return "98-SA";
	case mag_model_x68k: return "X68K";
	case mag_model_mps_new: return "MPS for PC-98";
	case mag_model_pc88: return "PC-88";
	case mag_model_mac: return "MAC";
	case mag_model_mps: return "MPS";
	}
	return "???";
}

void mag_cleanup(struct mag_desc *desc) {
	free(desc->yae);
}

void mag_decode(const struct mag_desc *desc, struct wuimg *img) {
	/* Decoding depends on 5 sections. These are the FlagA, FlagB, and
	 * Color sections in the file, plus an Action buffer and the image
	 * itself. The Action buffer must be initialized to zero, be one fourth
	 * the size of an image row (there's one byte for every double-word),
	 * and reused for every row.

	 * FlagA is read bit by bit in MS-to-LS order. If the bit is set, a
	 * byte is read from FlagB and XOR'ed into the current Action byte.
	 * Do nothing if the bit is zero.

	 * Read the current Action byte, and read the nibbles is MS-to-LS
	 * order. If the nibble is zero, read a 16-bit word from Color
	 * and write it to the output image. Otherwise, copy a previous word
	 * from the image, according to the table below.

		+-------+---+---++-------+---+----++-------+---+----+
		| Value | X | Y || Value | X |  Y || Value | X |  Y |
		+-------+---+---++-------+---+----++-------+---+----+
		| 1     | 1 | 0 || 6     | 0 |  2 || B     | 2 |  4 |
		| 2     | 2 | 0 || 7     | 1 |  2 || C     | 0 |  8 |
		| 3     | 4 | 0 || 8     | 2 |  2 || D     | 1 |  8 |
		| 4     | 0 | 1 || 9     | 0 |  4 || E     | 2 |  8 |
		| 5     | 1 | 1 || A     | 1 |  4 || F     | 0 | 16 |
		+-------+---+---++-------+---+----++-------+---+----+
	*/

	const size_t stride = desc->row_dwords;
	const size_t dwords = stride * img->h;
	uint16_t *dst = (uint16_t *)img->data;
	switch (desc->msx.screen) {
	case mag_msx2p_screen10:
	case mag_msx2p_screen11:
	case mag_msx2p_screen12:
		/* YKJ mode expands to twice the input data, so write
		 * into the middle of the image buffer. It'll be overwritten
		 * at the end. */
		dst = (uint16_t *)(img->data + dwords*4);
		break;
	default: break;
	}

	/* Put the Action row at the very end of the output buffer. It will be
	 * overwritten in the last iteration. */
	uint8_t *act = (uint8_t *)dst + dwords*4 - stride;
	// img mem is already zeroed
	//memset(act, 0, stride);
	const struct wuptr flag_a = desc->flag_a;
	const struct wuptr flag_b = desc->flag_b;

	const size_t color_len = desc->color.len/2;
	const uint8_t *color = desc->color.ptr;

	size_t b_pos = 0;
	size_t c_pos = 0;
	for (size_t y = 0; y < img->h; ++y) {
		for (size_t x = 0; x < stride; ++x) {
			const size_t n = y*stride + x;
			if (n/8 < flag_a.len && b_pos < flag_b.len) {
				const bool b = bit_get(flag_a.ptr, n);
				const uint8_t c = flag_b.ptr[b_pos];
				act[x] ^= c*b;
				b_pos += b;
			}
			const uint8_t c = act[x];
			for (size_t i = 0; i < 2; ++i) {
				const size_t pos = n*2 + i;
				uint8_t xx = 00;
				uint8_t yy = 00;
				switch ((c >> (4 - i*4)) & 0x0f) {
				case 0:
					if (c_pos < color_len) {
						memcpy(dst + pos,
							color + c_pos*2,
							sizeof(*dst));
						++c_pos;
					} else {
						// already zeroed
						//dst[pos] = 0;
					}
					continue;
				case 0x1: xx = 1; yy = 0; break;
				case 0x2: xx = 2; yy = 0; break;
				case 0x3: xx = 4; yy = 0; break;
				case 0x4: xx = 0; yy = 1; break;
				case 0x5: xx = 1; yy = 1; break;
				case 0x6: xx = 0; yy = 2; break;
				case 0x7: xx = 1; yy = 2; break;
				case 0x8: xx = 2; yy = 2; break;
				case 0x9: xx = 0; yy = 4; break;
				case 0xa: xx = 1; yy = 4; break;
				case 0xb: xx = 2; yy = 4; break;
				case 0xc: xx = 0; yy = 8; break;
				case 0xd: xx = 1; yy = 8; break;
				case 0xe: xx = 2; yy = 8; break;
				case 0xf: xx = 0; yy = 16; break;
				}
				const size_t look_back = yy*2*stride + xx;
				dst[pos] = (look_back <= pos)
					? dst[pos - look_back]
					: 0;
			}
		}
	}

	if (img->data != (uint8_t *)dst) {
		v9958_ykj_to_grb((upack1555_t *)img->data,
			(uint8_t *)dst, dwords, desc->yae);
	}
}

static bool deca_loader(const struct mag_desc *desc) {
	const struct wuptr *comm = &desc->comm;
	const size_t offset = 24;
	const uint8_t id[12] = "Deca loader ";
	if (comm->len >= offset + sizeof(id)) {
		return !memcmp(comm->ptr + offset, id, sizeof(id));
	}
	return false;
}

static struct wu_st read_pal(struct mag_desc *desc, struct wuimg *img,
struct mparser *mp, const bool is_yjk) {
	struct palette *pal = palette_new();
	if (!pal) {
		return WUERR_HERE(wu_alloc_error);
	}
	if (is_yjk) {
		desc->yae = pal;
	} else {
		wuimg_palette_set(img, pal);
	}

	const size_t entries = 1 << img->bitdepth;
	struct pix_rgb8 *buf = (struct pix_rgb8 *)mp_slice(mp,
		entries * sizeof(*buf));
	if (!buf) {
		return WUERR_HERE(wu_unexpected_eof);
	}

	uint8_t outbits, inbits;
	if (is_yjk) {
		// Scale from 3 to 5 bits, as required by ykj_to_grb()
		outbits = 5;
		inbits = 3;
	} else {
		img->bitrange = 4;
		switch (desc->code) {
		case mag_model_msx:
			if (img->bitdepth == 8) {
				img->bitrange = 8;
			} else if (!deca_loader(desc)) {
				img->bitrange = 3;
			}
			break;
		case mag_model_x68k: img->bitrange = 5; break;
		case mag_model_mac: img->bitrange = 8; break;
		case mag_model_pc88:
			break;
		default:
			if (img->bitdepth == 8) {
				img->bitrange = 8;
			}
			break;
		}
		// Shift down but don't scale, leave that to color management
		inbits = img->bitrange;
		outbits = inbits;
	}
	uint32_t outrange = bit_set32(outbits);
	uint32_t scale = (outrange << 8) / bit_set32(inbits) + 1;
	uint32_t shr = 8 - inbits;
	for (size_t i = 0; i < entries; ++i) {
		pal->color[i] = (struct pix_rgba8) {
			(uint8_t)(((buf[i].r >> shr) * scale) >> 8),
			(uint8_t)(((buf[i].g >> shr) * scale) >> 8),
			(uint8_t)(((buf[i].b >> shr) * scale) >> 8),
			(uint8_t)outrange,
		};
	}
	return WU_OK;
}

static struct wu_st get_dimensions(struct wuimg *img, const uint16_t x_left,
const uint16_t y_top, const uint16_t x_right, const uint16_t y_bottom) {
	// x_right and y_bottom are inclusive
	if (x_left <= x_right && y_top <= y_bottom) {
		const size_t ppb = 8u/img->bitdepth;
		const size_t left = (x_left / ppb) & ~3u;
		const size_t right = (x_right / ppb) & ~3u;
		img->w = (right - left + 4) * ppb;
		img->h = (size_t)y_bottom - y_top + 1;
		return WU_OK;
	}
	return wuerr(wu_invalid_header, "negative width or height");
}

struct wu_st mag_parse(struct mag_desc *desc, struct wuimg *img,
const struct wuptr mem) {
	/* MAKI02 header (after prev):
		Offset  Size    Name
		0       u8	ComputerModel[4]
		4       char    Comment[]        // 0x1a terminated
		?       char    Dummy[]          // 0x00 terminated

		0       u8      ModelCode
		+1      u8      MSXFlags
		+2      u8      ScreenMode
		+3      u16     XLeftEdge
		+5      u16     YTopEdge
		+7      u16     XRightEdge
		+9      u16     YBottomEdge
		+11     u32     FlagAOffset      // [1]
		+15     u32     FlagBOffset      // [1]
		+19     u32     FlagBSize
		+23     u32     ColorOffset      // [1]
		+27     u32     ColorSize
		+31     u8      Palette[]        // GRB order, variable size

	 * [1] Offset relative to the Dummy 0x00 terminator.
	*/

	struct mparser mp = mp_wuptr(mem);
	*desc = (struct mag_desc){0};
	const uint8_t magic[8] = {'M','A','K','I','0','2',' ',' '};
	const uint8_t *buf = mp_slice(&mp,
		sizeof(magic) + sizeof(desc->model));
	if (!buf) {
		return WUERR_HERE(wu_unexpected_eof);
	} else if (memcmp(buf, magic, sizeof(magic))) {
		return WUERR_HERE(wu_invalid_signature);
	}
	memcpy(desc->model, buf + sizeof(magic), sizeof(desc->model));

	if (!mp_upto(&mp, &desc->comm, 0x1a)
	|| !mp_upto(&mp, &desc->dummy, 0x00)) {
		return WUERR_HERE(wu_unexpected_eof);
	}
	const size_t null_pos = mp.pos - 1;

	buf = mp_slice(&mp, 31);
	if (!buf) {
		return WUERR_HERE(wu_unexpected_eof);
	}

	desc->code = buf[0];
	desc->screen_mode = buf[2];
	img->channels = 1;
	img->bitdepth = (desc->screen_mode & 0x80) ? 8 : 4;
	img->layout = pix_grba;
	bool is_yjk = false;
	bool load_pal = true;
	if (desc->code == mag_model_msx) {
		desc->msx.screen = buf[1] >> 4;
		desc->msx.interlace = !(buf[1] & 0x04); // ???
		switch (desc->msx.screen) {
		case mag_msx2_screen5:
			break;
		case mag_msx2_screen8:
			// telparia.com/fileFormatSamples/image/makichan/19DEZ2.MAG
			img->ratio = desc->msx.interlace ? 2 : 1;
			break;
		case mag_msx2_screen7:
		case mag_msx2_screen6:
			img->ratio = desc->msx.interlace ? 1 : 1/2.0;
			break;
		case mag_msx2p_screen12:
			load_pal = false;
			// fallthrough
		case mag_msx2p_screen10:
		case mag_msx2p_screen11:
			// PixelArt.v04/MAKICHAN/SCR12i/HAWAI.MAG
			// telparia.com/fileFormatSamples/image/makichan/TSUCHIIN.MAG
			img->ratio = desc->msx.interlace ? 2 : 1;
			is_yjk = true;
			break;
		default:
			return wuerr(wu_invalid_header, "unknown screen mode");
		}
	} else {
		img->ratio = ((desc->screen_mode & 0x81) == 0x01) ? 1/2.0 : 1;
	}

	struct wu_st st = get_dimensions(img,
		buf_endian16l(buf + 3),
		buf_endian16l(buf + 5),
		buf_endian16l(buf + 7),
		buf_endian16l(buf + 9));
	if (!wu_isok(st)) {
		return st;
	}

	if (desc->msx.screen == mag_msx2_screen6) {
		// telparia.com/fileFormatSamples/image/makichan/GUARDIAN.MAG
		// PixelArt.v03/MAKICHAN/ARR6i/
		img->w *= img->bitdepth / 2;
		img->bitdepth = 2;
	}

	const size_t stride = strip_base(img->w, img->bitdepth);
	const size_t bytes = stride * img->h;
	desc->row_dwords = stride / 4;
	const size_t dwords = bytes / 4;

	const size_t fa_size = strip_base(dwords, 1);
	const uint32_t fb_size = buf_endian32l(buf + 19);
	const uint32_t c_size = buf_endian32l(buf + 27);
	if (c_size & 1) {
		return wuerr(wu_invalid_header, "color section with odd size");
	} else if (fb_size > dwords || c_size > bytes) {
		return wuerr(wu_invalid_params,
			"we may have miscalculated the image bitdepth");
	}

	desc->flag_a = mp_avail_at(&mp, null_pos + buf_endian32l(buf + 11),
		fa_size);
	desc->flag_b = mp_avail_at(&mp, null_pos + buf_endian32l(buf + 15),
		fb_size);
	desc->color = mp_avail_at(&mp, null_pos + buf_endian32l(buf + 23),
		c_size);
	if (load_pal) {
		st = read_pal(desc, img, &mp, is_yjk);
		if (!wu_isok(st)) {
			return st;
		}
	}
	if (is_yjk) {
		img->w /= 8u / img->bitdepth;
		img->bitdepth = 16;
		if (!wuimg_bitfield_from_id(img, 0x555)) {
			return WUERR_HERE(wu_alloc_error);
		}
	}
	return wuerr_partial(
		desc->flag_a.len + desc->flag_b.len + desc->color.len,
		fa_size + fb_size + c_size);
}

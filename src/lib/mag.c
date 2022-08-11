#include "mag.h"
#include "raster/bit.h"
#include "raster/file.h"
#include "raster/fmt.h"
#include "raster/graphics_adapters.h"

/* Based on
https://mooncore.eu/bunny/txt/makichan.htm

 * With some fixes to make the MSX Pixel Art Collection and telparia.com
 * samples work. (Some images NSFW).
https://frs.badcoffee.info/MSXart.html
https://telparia.com/fileFormatSamples/image/makichan/

 * MAKI01 is implemented in maki.c, as it has almost nothing in common with
 * MAG/MAKI02.
*/

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
	free(desc->comment.data);
	free(desc->yjk_pal);
}

static size_t load_section(const struct mag_desc *desc,
const struct mag_section *src, void *restrict ptr) {
	fseek(desc->ifp, desc->null_pos + src->off, SEEK_SET);
	return fread(ptr, 1, src->size, desc->ifp);
}

size_t mag_decode(const struct mag_desc *desc, struct raw_img *img) {
	/* Decoding depends on 5 sections. These are the FlagA, FlagB, and
	 * Color sections in the file, plus an Action buffer and the image
	 * itself. The Action buffer must be one fourth the size of an image
	 * row, and initialized to zero.

	 * FlagA is read bit by bit in MS-to-LS order. If the bit is set, a
	 * byte is read from FlagB and XOR'ed into the current Action byte.
	 * The Action pointer is not advanced. Do nothing if the bit is zero.

	 * Read a byte from Action and advance the pointer. Read the nibbles is
	 * MS-to-LS order. If the nibble is zero, read a 16-bit word from Color
	 * and write it to the output image. Otherwise, copy a previous word
	 * from the image

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

	/* The biggest possible stream is when FlagA is all ones, and FlagB
	 * all zeroes. Each FlagA bit will cause a byte to be read from FlagB,
	 * and each byte will cause two 16-bit words to be read from Color.
	*/
	const size_t stride = desc->row_dwords;
	const size_t dwords = stride * img->h;
	uint8_t *buf = malloc(stride // Action row
		+ desc->flag_a.size // FlagA
		+ dwords); // FlagB maximum
	if (!buf) {
		return 0;
	}

	uint16_t *dst = malloc(dwords * 4);
	if (!dst) {
		free(buf);
		return 0;
	}

	/* Put Color at the end of the output buffer. The algorithm will work
	 * as long as Color doesn't have more bytes than it uses. (That'd have
	 * to be a really lousy encoder). */
	const size_t color_len = desc->color.size/2;
	uint16_t *color = dst + dwords*2 - color_len;

	uint8_t *act = buf;
	uint8_t *flag_a = buf + stride;
	uint8_t *flag_b = flag_a + desc->flag_a.size;

	memset(act, 0, stride);
	size_t read = load_section(desc, &desc->flag_a, flag_a)
		+ load_section(desc, &desc->flag_b, flag_b)
		+ load_section(desc, &desc->color, color);

	size_t b_pos = 0;
	size_t c_pos = 0;
	for (size_t y = 0; y < img->h; ++y) {
		for (size_t x = 0; x < stride; ++x) {
			const size_t n = y*stride + x;
			if (bit_get(flag_a, n)) {
				act[x] ^= flag_b[b_pos];
				++b_pos;
			}
			const uint8_t c = act[x];
			for (size_t i = 0; i < 2; ++i) {
				const size_t pos = n*2 + i;
				uint8_t xx, yy;
				switch ((c >> (4 - i*4)) & 0x0f) {
				case 0:
					dst[pos] = c_pos < color_len
						? color[c_pos] : 0;
					++c_pos;
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
	free(buf);

	switch (desc->msx.screen) {
	case mag_msx2p_screen10:
	case mag_msx2p_screen11:
	case mag_msx2p_screen12:
		if (raw_img_alloc_noverify(img)) {
			v9958_ykj_to_grb(img->data, (uint8_t *)dst, dwords,
				desc->yjk_pal);
		} else {
			read = 0;
		}
		free(dst);
		break;
	default:
		img->data = (uint8_t *)dst;
	}
	return read;
}

static bool deca_loader(const struct mag_desc *desc) {
	const struct mag_comment *comm = &desc->comment;
	const size_t offset = 24;
	const uint8_t id[12] = "Deca loader ";
	if (comm->text_len > offset + sizeof(id)) {
		return !memcmp(comm->data + offset, id, sizeof(id));
	}
	return false;
}

static enum wu_error read_pal(const struct mag_desc *desc,
struct raster_pal *pal, const size_t entries) {
	struct pix_rgb8 *buf = (struct pix_rgb8 *)(pal->color + entries)
		- entries;
	if (!fread(buf, sizeof(*buf) * entries, 1, desc->ifp)) {
		return wu_unexpected_eof;
	}

	uint8_t bits = 4;
	switch (desc->code) {
	case mag_model_msx:
		if (entries == 256) {
			bits = 8;
		} else if (!deca_loader(desc)) {
			bits = 3;
		}
		break;
	case mag_model_x68k: bits = 5; break;
	case mag_model_mac: bits = 8; break;
	case mag_model_pc88:
		break;
	default:
		if (entries == 256) {
			bits = 8;
		}
		break;
	}

	const int scale = (0xff << 8) / ((1 << bits) - 1) + 1;
	const int shr = (8 - bits);
	for (size_t i = 0; i < entries; ++i) {
		pal->color[i] = (struct pix_rgba8) {
			(uint8_t)(((buf[i].r >> shr) * scale) >> 8),
			(uint8_t)(((buf[i].g >> shr) * scale) >> 8),
			(uint8_t)(((buf[i].b >> shr) * scale) >> 8),
			0xff,
		};
	}
	return wu_ok;
}

static enum wu_error get_dimensions(struct raw_img *img, const unsigned x_left,
const unsigned y_top, const unsigned x_right, const unsigned y_bottom) {
	// x_right and y_bottom are inclusive
	if (x_left <= x_right && y_top <= y_bottom) {
		const size_t ppb = 8/img->bitdepth;
		const size_t left = (x_left / ppb) & ~3u;
		const size_t right = (x_right / ppb) & ~3u;
		img->w = (right - left + 4) * ppb;
		img->h = y_bottom - y_top + 1;
		return wu_ok;
	}
	return wu_invalid_header;
}

static enum wu_error read_comment(struct mag_desc *desc) {
	// FIXME: Merge with Pi's read_comment
	struct mag_comment *comm = &desc->comment;
	struct wugrow grow;
	const size_t max = 0x4000; // PixelArt.v03/MAKICHAN/MAGSCR7/CHO13.MAG
	comm->data = fileccpy(&grow, '\0', max, desc->ifp);
	if (comm->data) {
		uint8_t *p = memrchr(comm->data, 0x1a, grow.pos);
		comm->text_len = p ? (size_t)(p - comm->data) : grow.pos;
		comm->area_len = grow.pos;
		return wu_ok;
	}
	return wu_invalid_header;
}

enum wu_error mag_parse(struct mag_desc *desc, struct raw_img *img) {
	/* MAKI02 header (after prev):
		Offset  Size    Name
		0       u8	ComputerModel[4]
		4       char    Comment[]        // 0x1a 0x00 terminated

		-1      u8      Null             // End of comment
		0       u8      ModelCode
		+1      u8      ModelFlags
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

	 * [1] Offset relative to the 'Null' field.
	*/

	if (!fread(desc->model, sizeof(desc->model), 1, desc->ifp)) {
		return wu_unexpected_eof;
	}

	enum wu_error st = read_comment(desc);
	if (st != wu_ok) {
		return st;
	}
	desc->null_pos = ftell(desc->ifp) - 1;

	uint8_t buf[31];
	if (!fread(buf, sizeof(buf), 1, desc->ifp)) {
		return wu_unexpected_eof;
	}

	img->channels = 1;
	img->bitdepth = (buf[2] & 0x80) ? 8 : 4;
	img->layout = pix_grba;
	bool is_yjk = false;
	bool load_pal = true;
	desc->code = buf[0];
	if (desc->code == mag_model_msx) {
		desc->msx.screen = buf[1] >> 4;
		desc->msx.interlace = !(buf[1] & 0x04);
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
			img->used_bits = 5;
			is_yjk = true;
			break;
		default:
			return wu_invalid_header;
		}
	} else {
		img->ratio = ((buf[2] & 0x81) == 0x01) ? 1/2.0 : 1;
	}

	st = get_dimensions(img,
		buf_endian16(buf + 3, little_endian),
		buf_endian16(buf + 5, little_endian),
		buf_endian16(buf + 7, little_endian),
		buf_endian16(buf + 9, little_endian));
	if (st != wu_ok) {
		return st;
	}

	if (desc->msx.screen == mag_msx2_screen6) {
		// telparia.com/fileFormatSamples/image/makichan/GUARDIAN.MAG
		// PixelArt.v03/MAKICHAN/ARR6i/
		img->w *= 2;
		img->bitdepth = 2;
	}

	const size_t outstride = scanline_length(img->w, img->bitdepth, 1);
	const size_t bytes = outstride * img->h;
	desc->row_dwords = outstride / 4;
	const size_t dwords = bytes / 4;

	desc->flag_a.off = buf_endian32(buf + 11, little_endian);
	desc->flag_a.size = (uint32_t)scanline_length(dwords, 1, 1);
	desc->flag_b.off = buf_endian32(buf + 15, little_endian);
	desc->flag_b.size = buf_endian32(buf + 19, little_endian);
	desc->color.off = buf_endian32(buf + 23, little_endian);
	desc->color.size = buf_endian32(buf + 27, little_endian);
	if (desc->color.size & 1) {
		return wu_invalid_header;
	} else if (desc->flag_b.size > dwords || desc->color.size > bytes) {
		 // Probably got the wrong bitdepth
		return wu_invalid_params;
	}

	if (load_pal) {
		struct raster_pal *pal = malloc(sizeof(*pal));
		if (!pal) {
			return wu_alloc_error;
		}

		st = read_pal(desc, pal, 1 << img->bitdepth);
		if (st != wu_ok) {
			free(pal);
			return st;
		}

		if (is_yjk) {
			desc->yjk_pal = pal;
		} else {
			raw_img_palette_set(img, pal);
		}
	}

	if (is_yjk) {
		img->w /= 8 / img->bitdepth;
		img->channels = 3;
		img->bitdepth = 8;
	}
	return raw_img_verify(img);
}

enum wu_error mag_open(struct mag_desc *desc, FILE *ifp) {
	*desc = (struct mag_desc) {.ifp = ifp};
	const uint8_t magic[8] = "MAKI02  ";
	return fmt_sigcmp(magic, sizeof(magic), ifp);
}

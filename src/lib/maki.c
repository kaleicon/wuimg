// SPDX-License-Identifier: 0BSD
#include "misc/bit.h"
#include "raster/fmt.h"
#include "maki.h"

/* Based on
https://mooncore.eu/bunny/txt/makichan.htm

 * MAG/MAKI02 is implemented in mag.c, as the format has nothing in common with
 * MAKI01 besides the signature.
*/

static const unsigned short MAKI_W = 640;
static const unsigned short MAKI_H = 400;

const char * maki_version_str(enum maki_version version) {
	switch (version) {
	case maki_1a: return "MAKI01A";
	case maki_1b: return "MAKI01B";
	}
	return "???";
}

size_t maki_decode(const struct maki_desc *desc, struct wuimg *img) {
	/* Compressed data is composed of three sections.
	 * The first two are FlagA (1000 bytes) and FlagB (variable size),
	 * which are used to create a Mask buffer that is 8000 16-bit words
	 * in size.
	 * Bits are read in MS-to-LS order from FlagA. If a bit is one, a
	 * 16-bit big-endian word is read from FlagB and written to Mask,
	 * otherwise zero is written. Each word corresponds to a 4*4 bit
	 * block, which will make up an 8*4 pixel area.

	 * The third section is the Pixel area, and requires linearizing the
	 * bits in Mask. For each 1 bit, a byte is read from Pixel and written
	 * to the output, otherwise 0. Each nibble in this byte
	 * corresponds to a palette entry.

	 * When done, each row must be XOR'd with previous ones. For MAKI1A,
	 * the look-back distance is two rows. For MAKI1B, look-back is four
	 * rows. This obviously doesn't apply to the starting rows.
	*/

	if (!wuimg_alloc_noverify(img)) {
		return 0;
	}

	/* The section sizes fields are unreliable, so we'll allocate for the
	 * worst case. This would be where all FlagA bits are one, thus making
	 * FlagB the same size as Mask, and where all Mask bits are one, making
	 * the Pixel area contain the complete 4-bit raster. */
	const size_t flag_a_len = 1000;
	const size_t mask_len = flag_a_len * 8 * 2;
	const size_t row_len = MAKI_W / 2;
	const size_t raster_len = row_len * MAKI_H;

	/* Allocate a single buffer, and put Mask at the start, overlapping the
	 * FlagA section. Saves a measly 1000 bytes, but it's free. */
	const size_t alloc = mask_len * 2 + raster_len;
	uint8_t *buf = malloc(alloc);
	if (!buf) {
		return 0;
	}

	uint8_t *flag_a = buf + mask_len - flag_a_len;
	const size_t read_max = flag_a_len + mask_len + raster_len;
	const size_t read = fread(flag_a, 1, read_max, desc->ifp);
	if (read <= flag_a_len) {
		free(buf);
		return 0;
	}

	const uint16_t *flag_b = (uint16_t *)(flag_a + flag_a_len);
	size_t b_pos = 0;
	uint16_t *mask = (uint16_t *)buf;
	for (size_t i = 0; i < flag_a_len * 8; ++i) {
		uint16_t v = 0;
		if (bit_get(flag_a, i)) {
			v = endian16(flag_b[b_pos], big_endian);
			++b_pos;
		}
		mask[i] = v;
	}

	const uint8_t *pxl = (uint8_t *)(flag_b + b_pos);
	size_t p_pos = 0;
	for (size_t y = 0; y < MAKI_H; ++y) {
		const uint16_t *mask_row = mask + y/4 * row_len/4;
		const size_t tile_y = y % 4;
		for (size_t x = 0; x < row_len/4; ++x) {
			const uint16_t tile = mask_row[x];
			const size_t row = y * row_len;
			for (size_t tile_x = 0; tile_x < 4; ++tile_x) {
				uint8_t v = 0;
				if ((tile >> (15 - (tile_y*4 + tile_x))) & 1) {
					v = pxl[p_pos];
					++p_pos;
				}
				img->data[row + x*4 + tile_x] = v;
			}
		}
	}
	free(buf);

	const size_t look_back = ((desc->version == maki_1a) ? 2 : 4) * row_len;
	for (size_t i = look_back; i < MAKI_H * row_len; ++i) {
		img->data[i] ^= img->data[i - look_back];
	}
	return read;
}

enum wu_error maki_parse(struct maki_desc *desc, struct wuimg *img) {
	/* MAKI01 header (after magic bytes):
		Offset  Size    Name
		0       u8	ComputerModel[4]
		4       char    Comment[20]      // SHIFT JIS, of course
		24      u16     FlagBSize        // [1]
		26      u16     PixelASize       // [1][2]
		28      u16     PixelBSize       // [1][2]
		30      u16     ExtensionFlag
		32      u16     XOffset
		34      u16     YOffset
		36      u16     Width            // [3]
		38      u16     Height           // [3]
		40      u8      Palette[48]      // GRB order
		88
	 * [1] Apparently, they may contain junk.
	 * [2] The pixel area is split into A and B due to limitations on
	 *     16-bit systems. They're continuous so they can be treated as one.
	 * [3] The algorithm always outputs 640*480 pixels, so this is
	 *     informative only.
	*/
	uint16_t buf[8];
	if (!fread(desc->model, sizeof(desc->model), 1, desc->ifp)
	|| !fread(desc->comment, sizeof(desc->comment), 1, desc->ifp)
	|| !fread(buf, sizeof(buf), 1, desc->ifp)) {
		return wu_unexpected_eof;
	}

	desc->x = endian16(buf[4], big_endian);
	desc->y = endian16(buf[5], big_endian);

	img->w = MAKI_W;
	img->h = MAKI_H;
	img->channels = 1;
	img->bitdepth = 4;
	img->layout = pix_grba;
	img->ratio = (endian16(buf[3], big_endian) & 1) ? 1/2.0 : 1;
	struct raster_pal *pal = wuimg_palette_init(img);
	if (pal) {
		const enum wu_error st = fmt_load_pal(desc->ifp, pal,
			fmt_pal_rgb, 16);
		if (st == wu_ok) {
			return wuimg_verify(img);
		}
		return st;
	}
	return wu_alloc_error;
}

enum wu_error maki_open(struct maki_desc *desc, FILE *ifp) {
	*desc = (struct maki_desc) {.ifp = ifp};

	uint8_t sig[8];
	if (!fread(sig, sizeof(sig), 1, desc->ifp)) {
		return wu_unexpected_eof;
	}
	const uint8_t maki[6] = "MAKI01";
	if (!memcmp(sig, maki, sizeof(maki)) && sig[7] == ' ') {
		switch (sig[6]) {
		case maki_1a:
		case maki_1b:
			desc->version = sig[6];
			return wu_ok;
		}
		return wu_invalid_header;
	}
	return wu_invalid_header;
}

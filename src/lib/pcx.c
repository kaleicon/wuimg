// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2021 kaleido
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>

#include "misc/file.h"
#include "misc/math.h"
#include "misc/mem.h"
#include "raster/fmt.h"
#include "raster/graphics_adapters.h"
#include "raster/unpack.h"

#include "pcx.h"

/* It's like this format was intelligently designed to be terrible. */

static const size_t VGA_PAL_LEN = 256*3;

enum pcx_palette_source {
	pcx_no_pal,
	pcx_ega,
	pcx_file_header,
	pcx_vga,
};

const char * pcx_version_string(const enum pcx_version ver) {
	switch (ver) {
	case pcx_ver25: return "2.5";
	case pcx_ver28_egapal: return "2.8 with EGA palette";
	case pcx_ver28_nopal: return "2.8 with no palette";
	case pcx_paintbrush: return "Paintbrush for Windows";
	case pcx_ver30: return "3.0";
	}
	return "???";
}

static struct wu_st pcx_unpack_interleave(struct wuimg *img) {
	const size_t outstride = strip_length(img->w * img->channels, img->bitdepth, 0);
	unsigned char *dst = calloc(outstride, img->h);
	if (!dst) {
		return WUERR_HERE(wu_alloc_error);
	}
	const size_t plane_stride = strip_length(img->w, img->bitdepth,
		img->align_sh);
	const size_t row_stride = plane_stride * img->channels;
	const size_t instride = row_stride;
	const unsigned char *src = img->data;

	if (img->bitdepth == 1) {
		for (size_t y = 0; y < img->h; ++y) {
			bitplane_interleave_pack(dst + y*outstride,
				src + y*instride, img->w, img->channels,
				img->align_sh);
		}
	} else {
		for (size_t y = 0; y < img->h; ++y) {
			for (size_t z = 0; z < img->channels; ++z) {
				strip_spread(dst + y*outstride + z,
					src + y*row_stride + plane_stride*z,
					img->w, img->channels);
			}
		}
	}

	free(img->data);
	img->data = dst;
	if (img->mode == image_mode_palette) {
		img->bitdepth = img->channels;
		img->channels = 1;
	}
	img->align_sh = 0;
	return WU_OK;
}

static bool check_cga_mode(const struct pcx_desc *desc,
const struct wuimg *img) {
	/* Files with bitdepth == 2 can be in CGA mode, meaning bytes in the
	 * first two entries of the header palette are used to select one of
	 * CGA palettes, or can be in 'regular' mode, in which the header
	 * palette is a regular palette like with higher bitdepths. This being
	 * the PCX format means there's no indication of when was each meant to
	 * be used, and plenty of samples relying on either behaviour.

	 * Although some docs talk about strange heuristics like testing for
	 * common CGA resolutions or bits/planes combinations, here we opt for
	 * checking whether the last two entries are zeroed. This makes all
	 * the images in the FFmpeg samples display OK, as far as one can tell
	 * with a format that's undecidable to render.

	 * Care should be taken NOT to check the whole palette area for
	 * zeroes, as uncleared memory (or perhaps some weird header extension)
	 * will be found in there. */
	return img->bitdepth == 2 && !memchk(desc->file_pal + 6, 0, 6);
}

static bool load_palette(const struct pcx_desc *desc,
struct wuimg *img, const struct pix_rgb8 *pal_data) {
	// Take a deep breath...
	struct palette *pal = wuimg_palette_init(img);
	if (!pal) {
		return false;
	}

	const size_t entries = desc->entries;
	const unsigned char *file_pal = desc->file_pal;
	if (entries == 2) {
		/* PC Paintbrush will display a dialog asking the user whether
		 * to open 1-bit 1-plane files as B&W or using the header
		 * palette, so one shouldn't get too stressed about which way
		 * is correct.
		 * Here, to use the file palette, we check if
		 *  · the file version allows a palette
		 *  · palette_type is non-zero
		 *  · entries are different
		 * This seems to work rather well for all samples. */
		if (pal_data && desc->palette_type
		&& memcmp(pal_data, pal_data + 1, sizeof(*pal_data))) {
			palette_from_rgb8(pal, pal_data, entries);
		} else {
			/* Beware when testing: imagemagick renders monochrome
			 * opposite from ffmpeg. */
			pal->color[0] = (struct pix_rgba8){0x00, 0x00, 0x00, 0xff};
			pal->color[1] = (struct pix_rgba8){0xff, 0xff, 0xff, 0xff};
		}
	} else if (check_cga_mode(desc, img)) {
		img->bitrange = 2;
		unsigned palnum;
		bool intensity, colorburst;
		if (desc->palette_type) {
			intensity = imax(file_pal[4], file_pal[5]) > 200;
			palnum = file_pal[4] > file_pal[5] ? 0 : 1;
			colorburst = true;
		} else {
			const int status = file_pal[3];
			intensity = (status >> 5) & 1;
			palnum = (status >> 6) & 1;
			colorburst = (status >> 7) & 1;
		}

		const uint8_t cga_bg = file_pal[0] >> 4;
		pal->color[0] = cga_palette(cga_bg);
		if (colorburst) {
			// Palettes 0 and 1
			const size_t offset = palnum + intensity*8;
			for (size_t i = 1; i < 4; ++i) {
				const size_t idx = offset + i*2;
				pal->color[i] = cga_palette(idx);
			}
		} else {
			// Unofficial palette 2
			const size_t offset = intensity*8;
			for (size_t i = 1; i < 4; ++i) {
				// Fancy way of indexing '{3, 4, 7}'
				const size_t idx = offset + i*2 + (i & 1);
				pal->color[i] = cga_palette(idx);
			}
		}
	} else if (pal_data) { // Header or trailing palette
		palette_from_rgb8(pal, pal_data, entries);
	} else { // Standard EGA palette (CGA)
		img->bitrange = 2;
		for (size_t i = 0; i < entries; ++i) {
			pal->color[i] = cga_palette(i);
		}
	}
	return true;
}

static struct wu_st looking_for_lost_pauline(struct pcx_desc *desc,
struct wuimg *img, const unsigned char *restrict vga_id,
const size_t rle_remaining) {
	if (img->bitdepth * img->channels > 8) {
		return WU_OK;
	}

	enum pcx_palette_source pal_src = pcx_no_pal;
	if (img->bitdepth == 8) {
		if (rle_remaining > VGA_PAL_LEN && *vga_id == 0x0c) {
			pal_src = pcx_vga;
		}
	} else {
		switch (desc->version) {
		case pcx_ver25:
		case pcx_ver28_nopal:
			pal_src = pcx_ega;
			break;
		default:
			pal_src = pcx_file_header;
		}
	}

	const void *pal_data = NULL;
	switch (pal_src) {
	case pcx_no_pal:
		return WU_OK;
	case pcx_ega:
		break;
	case pcx_file_header:
		pal_data = desc->file_pal;
		break;
	case pcx_vga:
		pal_data = vga_id + 1;
		break;
	}

	if (!load_palette(desc, img, pal_data)) {
		return WUERR_HERE(wu_alloc_error);
	}
	return WU_OK;
}

static size_t rle_decode(unsigned char *restrict dst, const size_t dst_len,
const unsigned char *restrict rle, const size_t rle_len) {
	size_t d = 0;
	size_t r = 0;
	const uint8_t mask = 0xc0;
	while (r < rle_len) {
		/* Don't increase `r` until we've written the value, as we
		 * might skip the palette byte if we break out. PCX sucks. */
		const uint8_t packet = rle[r];
		if (packet >= mask) {
			const size_t run_len = packet - mask;
			if (r + 1 >= rle_len || d + run_len > dst_len) {
				break;
			}
			memset(dst + d, rle[r+1], run_len);
			d += run_len;
			r += 2;
		} else {
			if (d >= dst_len) {
				break;
			}
			dst[d] = packet;
			++d;
			++r;
		}
	}
	return r;
}

struct wu_st pcx_decode(struct pcx_desc *desc, struct wuimg *img) {
	const size_t dims = strip_length(img->w, img->bitdepth, img->align_sh)
		* img->channels * img->h;
	img->data = malloc(dims);
	if (!img->data) {
		return WUERR_HERE(wu_alloc_error);
	}

	desc->mp.pos = 128;
	struct wuptr src;
	size_t r;
	if (desc->compressed) {
		// e.g. 0xc1 0x01 0xc1 0x02 -> 0x01 0x02
		src = mp_avail(&desc->mp, dims*2 + VGA_PAL_LEN + 1);
		r = rle_decode(img->data, dims, src.ptr, src.len);
	} else {
		src = mp_avail(&desc->mp, dims + VGA_PAL_LEN + 1);
		r = zumin(dims, src.len);
		memcpy(img->data, src.ptr, r);
	}
	if (!r) {
		return WUERR_HERE(wu_unexpected_eof);
	}
	struct wu_st st = looking_for_lost_pauline(desc, img,
		src.ptr + r, src.len - r);
	if (!wu_isok(st)) {
		return st;
	}

	if (img->channels > 1) {
		st = pcx_unpack_interleave(img);
		if (!wu_isok(st)) {
			return st;
		}
	}
	return wuerr(wuimg_verify(img), NULL);
}

struct wu_st pcx_read_header(struct pcx_desc *desc, struct wuimg *img) {
	/* Header continuation
		Offset  Size    Name
		0       BYTE    BitsPerPixel;   // 1, 2, 4, or 8
		1       WORD    XStart;
		3       WORD    YStart;
		5       WORD    XEnd;
		7       WORD    YEnd;
		9       WORD    HorzRes;
		11      WORD    VertRes;
		13      BYTE    EGAPalette[48];
		61      BYTE    Reserved1;
		62      BYTE    NumBitPlanes;   // 1, 2, 3, or 4
		63      WORD    BytesPerLine;   // Line of a single plane
		65      WORD    PaletteType;    // CGA palette interpretation. [1]
		67      WORD    HorzScreenSize; // [2]
		69      WORD    VertScreenSize; // [2]
		71      BYTE    Reserved2[54];
		125

	[1] Many docs claim it can only be 1 or 2, but apparently it was 0
		before PC Paintbrush 4.0, and any of the three afterwards.[3]
		What practical difference 1 or 2 make it's not yet clear to me.
		Non-CGA files meanwhile can have random values.
	[2] Fields added after version 4.0, previously part of Reserved2.[3]
	[3] As befitting this format, version 4 is just when the version field
		stopped being updated.
	*/

	const uint8_t *hdr = mp_slice(&desc->mp, 71);
	if (!hdr) {
		return WUERR_HERE(wu_unexpected_eof);
	}

	desc->horz_res = buf_endian16(hdr + 9, little_endian);
	desc->vert_res = buf_endian16(hdr + 11, little_endian);
	desc->file_pal = hdr + 13;
	desc->palette_type = buf_endian16(hdr + 65, little_endian);
	desc->horz_screen = buf_endian16(hdr + 67, little_endian);
	desc->vert_screen = buf_endian16(hdr + 69, little_endian);

	const uint8_t bitdepth = hdr[0];
	const uint16_t xstart = buf_endian16(hdr + 1, little_endian);
	const uint16_t ystart = buf_endian16(hdr + 3, little_endian);
	const uint16_t xend = buf_endian16(hdr + 5, little_endian);
	const uint16_t yend = buf_endian16(hdr + 7, little_endian);
	const uint8_t planes = hdr[62];
	const uint16_t bytes_per_line = buf_endian16(hdr + 63, little_endian);

	desc->entries = 1 << (planes * bitdepth);
	const int height = yend - ystart + 1;
	int width = xend - xstart + 1;
	if (width < 1) {
		/* Kludge for the broken files in
		 * https://github.com/jsummers/deark/issues/79 */
		width = xend;
	}
	if (width < 1 || height < 1) {
		return wuerr(wu_invalid_header, "Zero or negative dimensions");
	}

	switch (bitdepth) {
	case 1: case 8:
		if (planes < 1 || planes > 4) {
			return wuerr(wu_invalid_header,
				"Plane number out of range");
		}
		break;
	case 2: case 4:
		if (planes != 1) {
			return wuerr(wu_invalid_header,
				"Planes != 1 for depths 2 and 4");
		}
		break;
	default:
		return wuerr(wu_invalid_header, "Bad bitdepth");
	}
	img->w = (size_t)width;
	img->h = (size_t)height;
	img->channels = planes;
	img->bitdepth = bitdepth;
	img->align_sh = strip_alignment(bytes_per_line, img->w, img->bitdepth);
	if (img->align_sh < 0 || img->align_sh > 3) {
		return wuerr(wu_invalid_header, "Bizarre alignment");
	}
	return WU_OK;
}

struct wu_st pcx_open_file(struct pcx_desc *desc, const struct wuptr mem,
const bool word_for_dos_variant) {
	/* PCX header:
		Offset  Size    Name
		0	BYTE	IdentifierByte; // Always 0x0A [1]
		1	BYTE	Version;
		2	BYTE	Encoding;       // 0 or 1 [2]
		3
	 * [1] Except for Word for DOS screen captures, where it's 0xCD
	 * [*] The only valid value is 1, meaning RLE encoding, and virtually
	 *     all files in the wild follow this. Alas, imagemagick allows
	 *     creating uncompressed files with Encoding = 0, while
	 *     graphicsmagick does that by default.
	*/

	desc->mp = mp_wuptr(mem);
	if (desc->mp.len > 128) {
		const uint8_t *sig = mp_slice(&desc->mp, 3);
		if (sig) {
			desc->version = sig[1];
			desc->compressed = sig[2];
			switch (sig[0]) {
			case 0x0a:
				if (sig[2] <= 1) {
					switch (sig[1]) {
					case pcx_ver25:
					case pcx_ver28_egapal:
					case pcx_ver28_nopal:
					case pcx_paintbrush:
					case pcx_ver30:
						return WU_OK;
					}
				}
				break;
			case 0xcd:
				if (word_for_dos_variant && sig[2] == 1
				&& sig[1] == pcx_ver30) {
					return WU_OK;
				}
				break;
			}
			return WUERR_HERE(wu_invalid_signature);
		}
	}
	return WUERR_HERE(wu_unexpected_eof);
}


void dcx_free(struct dcx_desc *desc) {
	free(desc->off);
}

struct wu_st dcx_set_file(const struct dcx_desc *dcx, struct pcx_desc *pcx,
const uint32_t i) {
	if (i < dcx->nr) {
		return pcx_open_file(pcx, wuptr_mem(
			dcx->mp.mem + dcx->off[i],
			dcx->off[i + 1] - dcx->off[i]
		), false);
	}
	return WUERR_HERE(wu_invalid_params);
}

struct wu_st dcx_open_file(struct dcx_desc *d, const struct wuptr mem) {
	/* Why would anyone use the most device dependent file format ever for
	 * sending documents is beyond me.

	 * DCX header:
		Offset  Size    Name
		0       DWORD   Identifier
		4       DWORD   PageTable[] // PCX offsets. Max 1024 entries

	 * PageTable is 0-terminated, so this format also manages to suck
	 * despite using only two fields. It's amazing.
	*/

	d->mp = mp_wuptr(mem);
	d->off = NULL;
	const uint8_t sig[] = {0xb1, 0x68, 0xde, 0x3a};
	enum wu_error st = fmt_sigcmp_mem(sig, sizeof(sig), &d->mp);
	if (st == wu_ok) {
		const struct wuptr p = mp_remaining(&d->mp);
		const size_t max = zumin(1024, p.len/4);
		d->off = malloc(sizeof(*d->off) * (max + 1));
		if (d->off) {
			uint32_t prev = 0;
			for (d->nr = 0; d->nr < max; ++d->nr) {
				const uint32_t pos = buf_endian32(
					p.ptr + d->nr*4, little_endian);
				if (!pos || pos >= d->mp.len
				|| (d->nr && pos <= prev)) {
					break;
				}
				d->off[d->nr] = pos;
				prev = pos;
			}
			if (d->nr) {
				d->off[d->nr] = (uint32_t)d->mp.len;
			} else {
				st = wu_unexpected_eof;
			}
		} else {
			st = wu_alloc_error;
		}
	}
	return WUERR_HERE(st);
}

/* Ok bye */

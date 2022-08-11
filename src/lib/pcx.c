#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>

#include "raster/file.h"
#include "raster/fmt.h"
#include "raster/graphics_adapters.h"
#include "raster/mem.h"
#include "raster/unpack.h"

#include "pcx.h"

/* It's like this format was intelligently designed to be terrible. */

static const size_t RLE_MAX_RUN = 0x3f;
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

static size_t pcx_unpack_interleave(struct raw_img *img) {
	const bool has_pal = (img->mode == image_mode_palette);
	const size_t comps = (has_pal) ? 1 : img->channels;

	const size_t instride = scanline_length(img->w, img->bitdepth,
		img->align_sh) * img->channels;
	const size_t outstride = img->w * comps;
	unsigned char *dst = malloc(outstride * img->h);
	if (!dst) {
		return 0;
	}

	for (size_t y = 0; y < img->h; ++y) {
		vga_interleave(dst + y*outstride, img->data + y*instride,
			img->w, 1, img->channels, img->bitdepth, 1 << img->align_sh,
			has_pal);
	}

	free(img->data);
	img->data = dst;
	if (has_pal) {
		img->channels = 1;
		img->bitdepth = 8;
	}
	img->align_sh = 0;
	return 1;
}

static bool check_cga_mode(const struct pcx_desc *desc,
const struct raw_img *img) {
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
struct raw_img *img, const struct pix_rgb8 *pal_data) {
	// Take a deep breath...
	struct raster_pal *pal = raw_img_palette_init(img);
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
			raster_pal_from_rgb8(pal, pal_data, entries);
		} else {
			/* Beware when testing: imagemagick renders monochrome
			 * opposite from ffmpeg. */
			pal->color[0] = (struct pix_rgba8){0x00, 0x00, 0x00, 0xff};
			pal->color[1] = (struct pix_rgba8){0xff, 0xff, 0xff, 0xff};
		}
	} else if (check_cga_mode(desc, img)) {
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
		raster_pal_from_rgb8(pal, pal_data, entries);
	} else { // Standard EGA palette (CGA)
		for (size_t i = 0; i < entries; ++i) {
			pal->color[i] = cga_palette(i);
		}
	}
	return true;
}

static enum wu_error looking_for_lost_pauline(struct pcx_desc *desc,
struct raw_img *img, const unsigned char *restrict vga_id,
const size_t rle_remaining) {
	if (img->bitdepth * img->channels > 8) {
		return wu_ok;
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
		return wu_ok;
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
		return wu_alloc_error;
	}
	return wu_ok;
}

static size_t rle_decode(unsigned char *restrict dst, const size_t dst_len,
const unsigned char *restrict rle, const size_t rle_len) {
	size_t d = 0;
	size_t r = 0;
	const uint8_t mask = 0xc0;
	while (d < dst_len && r < rle_len) {
		const unsigned char packet = rle[r];
		++r;
		if (packet >= mask) {
			if (r >= rle_len) {
				break;
			}
			const size_t run_len = packet - mask;
			memset(dst + d, rle[r], run_len);
			d += run_len;
			++r;
		} else {
			dst[d] = packet;
			++d;
		}
	}
	return r;
}

size_t pcx_decode(struct pcx_desc *desc, struct raw_img *img) {
	const size_t dims = scanline_length(img->w, img->bitdepth, img->align_sh)
		* img->channels * img->h;
	// Add padding to save on a range check.
	img->data = malloc(dims + RLE_MAX_RUN);
	if (!img->data) {
		return 0;
	}

	desc->mp.pos = 128;
	struct wuptr rle = mp_next_remaining(&desc->mp,
		zumin(dims*2 + VGA_PAL_LEN + 1, desc->rle_len));
	if (!rle.len) {
		return 0;
	}

	const size_t r = rle_decode(img->data, dims, rle.ptr, rle.len);
	const enum wu_error fail = looking_for_lost_pauline(desc, img,
		rle.ptr + r, rle.len - r);
	if (fail != wu_ok) {
		return 0;
	}

	bool ok = true;
	if (img->channels > 1) {
		ok = pcx_unpack_interleave(img);
	}
	return ok && raw_img_verify(img) == wu_ok;
}

static enum wu_error validate_header(struct pcx_desc *desc, struct raw_img *img,
const uint8_t bitdepth, const int width, const int height,
const uint8_t planes, const uint16_t bytes_per_line,
const uint16_t palette_type) {
	switch (bitdepth) {
	case 1: case 8:
		if (planes < 1 || planes > 4) {
			return wu_invalid_header;
		}
		break;
	case 2: case 4:
		if (planes != 1) {
			return wu_invalid_header;
		}
		break;
	default:
		return wu_invalid_header;
	}

	if (width < 1 || height < 1) {
		return wu_invalid_header;
	}

	img->w = (size_t)width;
	img->h = (size_t)height;
	img->channels = planes;
	img->bitdepth = bitdepth;
	img->align_sh = scanline_alignment(bytes_per_line, img->w, img->bitdepth);
	if (img->align_sh < 0 || (1 << img->align_sh) > 8) {
		return wu_invalid_header;
	}

	desc->palette_type = palette_type;
	desc->bytes_per_line = bytes_per_line;
	desc->entries = 1 << (img->bitdepth * img->channels);
	return wu_ok;
}

enum wu_error pcx_read_header(struct pcx_desc *desc, struct raw_img *img) {
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

	const uint8_t *header1 = mp_next_slice(&desc->mp, 13); // Bytes 0 to 13
	desc->file_pal = mp_next_slice(&desc->mp, 48);
	const uint8_t *header2 = mp_next_slice(&desc->mp, 10); // Bytes 61 to 71
	if (!header2) {
		return wu_unexpected_eof;
	}

	const int xstart = buf_endian16(header1 + 1, little_endian);
	const int ystart = buf_endian16(header1 + 3, little_endian);
	const int xend = buf_endian16(header1 + 5, little_endian);
	const int yend = buf_endian16(header1 + 7, little_endian);
	const int width = xend - xstart + 1;
	const int height = yend - ystart + 1;

	desc->horz_res = buf_endian16(header1 + 9, little_endian);
	desc->vert_res = buf_endian16(header1 + 11, little_endian);
	desc->horz_screen = buf_endian16(header2 + 6, little_endian);
	desc->vert_screen = buf_endian16(header2 + 8, little_endian);
	return validate_header(desc, img, header1[0], width, height,
		header2[1],
		buf_endian16(header2 + 2, little_endian),
		buf_endian16(header2 + 4, little_endian));
}

enum wu_error pcx_open_file(struct pcx_desc *desc, const struct map_info *mm) {
	/* PCX header:
		Offset  Size    Name
		0	BYTE	IdentifierByte; // Always 0x0A
		1	BYTE	Version;
		2	BYTE	Encoding;       // Always 1
		3
	*/

	desc->mp = mp_parser_mem(mm->len, mm->data);
	if (desc->mp.len > 128) {
		const uint8_t *sig = mp_next_slice(&desc->mp, 3);
		if (sig) {
			if (sig[0] == 0x0a && sig[2] == 1) {
				switch (sig[1]) {
				case pcx_ver25:
				case pcx_ver28_egapal:
				case pcx_ver28_nopal:
				case pcx_paintbrush:
				case pcx_ver30:
					desc->rle_len = desc->mp.len - 128;
					desc->version = sig[1];
					return wu_ok;
				}
			}
			return wu_invalid_signature;
		}
	}
	return wu_unexpected_eof;
}


void dcx_free(struct dcx_desc *desc) {
	free(desc->off);
}

enum wu_error dcx_set_file(const struct dcx_desc *dcx, struct pcx_desc *pcx,
const uint32_t i) {
	struct map_info mm = {
		.len = dcx->off[i + 1] - dcx->off[i],
		.data = dcx->mp.mem + dcx->off[i],
	};
	return pcx_open_file(pcx, &mm);
}

enum wu_error dcx_open_file(struct dcx_desc *d, const struct map_info *mm) {
	/* Why would anyone use the most device dependent file format ever for
	 * sending documents is beyond me.

	 * DCX header:
		Offset  Size    Name
		0       DWORD   Identifier
		4       DWORD   PageTable[] // PCX offsets. 1024 entries max

	 * PageTable is 0 terminated, so this format manages to be bad despite
	 * using only two fields. It's amazing.
	*/

	d->mp = mp_parser_mem(mm->len, mm->data);
	d->off = NULL;
	const uint8_t sig[] = {0xb1, 0x68, 0xde, 0x3a};
	enum wu_error st = fmt_sigcmp_mem(sig, sizeof(sig), &d->mp);
	if (st == wu_ok) {
		const size_t max = 1023;
		d->off = malloc(sizeof(*d->off) * (max + 1));
		if (d->off) {
			const struct wuptr p = mp_next_remaining(&d->mp,
				SIZE_MAX);
			for (d->nr = 0; d->nr < max && d->nr*4 < p.len; ++d->nr) {
				d->off[d->nr] = buf_endian32(p.ptr + d->nr*4,
					little_endian);
				if (!d->off[d->nr]
				|| d->off[d->nr] >= d->mp.len
				|| (d->nr && d->off[d->nr] <= d->off[d->nr - 1])) {
					break;
				}
			}
			if (d->nr) {
				d->off[d->nr] = (uint32_t)d->mp.len;
			} else {
				free(d->off);
				st = wu_unexpected_eof;
			}
		} else {
			st = wu_alloc_error;
		}
	}
	return st;
}

/* Ok bye */

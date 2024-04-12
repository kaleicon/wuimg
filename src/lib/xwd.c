// SPDX-License-Identifier: 0BSD
#include "misc/bit.h"
#include "misc/common.h"
#include "misc/math.h"
#include "raster/fmt.h"

#include "xwd.h"
/* Some definitions after digging through many X headers. May be wrong:
 * - XYBitmap means a 1-bit min-is-white image. XYPixmap means a planar image
 *   ZPixmap means an image with interleaved channels.
 * - Visual Class is the colormap type. The ones with lower bit 0 used a fixed
 *   palette/mask, otherwise it was dynamic. This is an artifact of the moment
 *   the dump was made. Each pair can be treated the same when decoding, as the
 *   palette/mask is always included.
 * - The header specifies both word-endianness and bit-endianness.
 *   Bit-endianness seems only relevant for 1-bit images. Word-endianness
 *   affects images with bit depths > 8.
 * - Color order, in big-endian, is RGB for 24-bit images, XRGB for 32-bit.
 * And as anything related to the Xserver, all of the above may not apply for
 * particular images.
*/

enum xwd_order { // Whether byte or bit order
	xwd_lsb = 0,
	xwd_msb = 1,
};

const char * xwd_version_str(const enum xwd_version version) {
	switch (version) {
	case xwd_x10: return "X10";
	case xwd_x11: return "X11";
	}
	return "???";
}

const char * xwd_format_str(const enum xwd_format format) {
	switch (format) {
	case xwd_xybitmap: return "XYBitmap";
	case xwd_xypixmap: return "XYPixmap";
	case xwd_zpixmap: return "ZPixmap";
	}
	return "???";
}

const char * xwd_visual_str(const enum xwd_visual_class visual) {
	switch (visual) {
	case xwd_static_gray: return "StaticGray";
	case xwd_gray_scale: return "GrayScale";
	case xwd_static_color: return "StaticColor";
	case xwd_pseudo_color: return "PseudoColor";
	case xwd_true_color: return "TrueColor";
	case xwd_direct_color: return "DirectColor";
	}
	return "???";
}

void xwd_cleanup(struct xwd_desc *desc) {
	wustr_free(&desc->win.name);
}

size_t xwd_decode(const struct xwd_desc *desc, struct wuimg *img) {
	size_t read = 0;
	if (wuimg_alloc_noverify(img)) {
		read = fmt_load_raster_swap(img, desc->ifp, desc->byte_endian);
		if (img->bitdepth == 1 && desc->bit_endian == little_endian) {
			// Reverse bit order
			uint8_t *data = img->data;
			for (size_t i = 0; i < read; ++i) {
				data[i] = bit_rev8(data[i]);
			}
		}
	}
	return read;
}

#define XWD_PAL_ENTRY_SIZE 12
static enum wu_error load_palette(struct xwd_desc *desc, struct wuimg *img,
const uint32_t ncolors) {
	if (!ncolors || ncolors > 256) {
		return wu_invalid_header;
	}
	/* XWD palette entry:
		Offset  Type    Name
		0       u32     Pixel  // Entry number
		4       u16     RGB[3]
		10      u8      Flags  // Apparently, flags for libx11
		11      u8      _pad
		12
	 * Neither Pixel nor Flags seem relevant for decoding.
	*/

	uint8_t xwd_pal[XWD_PAL_ENTRY_SIZE*256];
	if (!fread(xwd_pal, XWD_PAL_ENTRY_SIZE*ncolors, 1, desc->ifp)) {
		return wu_unexpected_eof;
	}
	struct raster_pal *pal = img->u.palette;
	for (size_t i = 0; i < ncolors; ++i) {
		const size_t base = i*XWD_PAL_ENTRY_SIZE;
		/* FIXME: Support 16-bit palettes.
		 * Meanwhile, pick the most significant byte. */
		pal->color[i].r = xwd_pal[base + 4];
		pal->color[i].g = xwd_pal[base + 6];
		pal->color[i].b = xwd_pal[base + 8];
		pal->color[i].a = 0xff;
	}
	return wu_ok;
}

static enum wu_error get_window_name(struct xwd_desc *desc) {
	struct wustr *name = &desc->win.name;
	if (name->len) {
		if (!wustr_malloc(name, name->len)) {
			return wu_alloc_error;
		}
		if (!fread(name->str, name->len, 1, desc->ifp)) {
			return wu_unexpected_eof;
		}
		if (!name->str[name->len-1]) { // ignore null byte
			--name->len;
		}
	}
	return wu_ok;
}

static enum wu_error validate_header(struct xwd_desc *desc, struct wuimg *img,
const enum xwd_format format, const uint32_t depth, const uint32_t xoffset,
const uint32_t byte_order, const uint32_t bit_align, const uint32_t bit_order,
const uint32_t pix_align, const uint32_t bpp,
const enum xwd_visual_class visual_class, const uint32_t mask[static 3]) {
	switch (bit_align) {
	case 8: case 16: case 32: break;
	default: return wu_invalid_header;
	}
	switch (pix_align) {
	case 8: case 16: case 32: break;
	default: return wu_invalid_header;
	}

	switch (format) {
	case xwd_xybitmap:
		wuimg_align(img, (uint8_t)(bit_align/8));
		break;
	case xwd_xypixmap:
		return wu_unsupported_feature;
	case xwd_zpixmap:
		wuimg_align(img, (uint8_t)(pix_align/8));
		break;
	default:
		return wu_invalid_header;
	}
	desc->format = format;

	if (xoffset) {
		return wu_unsupported_feature;
	}

	switch (byte_order) {
	case xwd_lsb: desc->byte_endian = little_endian; break;
	case xwd_msb: desc->byte_endian = big_endian; break;
	default: return wu_invalid_header;
	}
	switch (bit_order) {
	case xwd_lsb: desc->bit_endian = little_endian; break;
	case xwd_msb: desc->bit_endian = big_endian; break;
	default: return wu_invalid_header;
	}

	bool paletted = false;
	switch (visual_class) {
	case xwd_static_color: case xwd_pseudo_color:
		paletted = true;
		// fallthrough
	case xwd_static_gray: case xwd_gray_scale:
		switch (bpp) {
		case 1: img->attr = pix_inverted; break;
		case 8: break;
		default: return wu_invalid_header;
		}
		if (depth > bpp) {
			return wu_invalid_header;
		}
		img->channels = 1;
		img->bitdepth = (uint8_t)bpp;
		if (paletted) {
			img->attr = pix_normal;
			wuimg_palette_init(img);
		} else {
			img->used_bits = (uint8_t)depth;
		}
		break;
	case xwd_true_color: case xwd_direct_color:
		switch (bpp) {
		case 16:
			/* Technically, we shouldn't care about the depth, as
			 * we extract according to the masks. */
			switch (depth) {
			case 15: case 16: break;
			default: return wu_invalid_header;
			}
			img->channels = 1;
			img->bitdepth = 16;
			break;
		case 24:
			if (bpp != depth) {
				return wu_invalid_header;
			}
			img->channels = 3;
			img->bitdepth = 8;
			break;
		case 32:
			img->channels = 4;
			img->bitdepth = 8;
			switch (depth) {
			case 24: case 32: break;
			default: return wu_invalid_header;
			}
			break;
		default:
			return wu_invalid_header;
		}

		struct bitfield *bf = wuimg_bitfield_init(img);
		if (!bf) {
			return wu_alloc_error;
		}
		if (!bitfield_from_mask(bf, mask, 3, (uint8_t)bpp,
		desc->byte_endian)) {
			return wu_invalid_header;
		}
		break;
	}
	desc->bpp = (uint8_t)bpp;
	desc->depth = (uint8_t)depth;
	desc->visual = visual_class;
	img->alpha = alpha_ignore;
	return wu_ok;
}

enum wu_error xwd_parse(struct xwd_desc *desc, struct wuimg *img) {
	/* XWD header (after HeaderSize and Version):
		Offset  Type    Name
		0       u32     Format
		4       u32     Depth       // Image bit depth, or meaningful bits
		8       u32     Width
		12      u32     Height
		16      u32     XOffset     // X offset in bits. For Bitmaps
		20      u32     ByteOrder   // Endianness of words
		24      u32     BitAlign    // Bit alignment of scanline. For Bitmaps
		28      u32     BitOrder    // Endianness of bits. For XYBitmap
		32      u32     PixAlign    // Bit alignment of scanline. For Pixmaps
		36      u32     BPP         // Bits used by each pixel
		40      u32     LineBytes
		44      u32     VisualClass
		48      u32     RGBMasks[3] // Masks for True/Direct color
		60      u32     BitsPerRGB  // Bits used by each mask
		64      u32     CMEntries   // Nb of used colors in colormap? Unused
		68      u32     NColors     // Nb of colormap entries
		72      u32     WindowW
		76      u32     WindowH
		80      u32     WindowX
		84      u32     WindowY
		88      u32     BorderWidth
		92      u8      WindowName[] // null-terminated
		HeaderSize

	 * Afterwards comes the palette, then the image data.
	*/
	uint32_t header[23];
	if (!fread(header, sizeof(header), 1, desc->ifp)) {
		return wu_unexpected_eof;
	}
	endian_loop32(header, big_endian, ARRAY_LEN(header));
	img->w = header[2];
	img->h = header[3];
	enum wu_error st = validate_header(desc, img, header[0],
		header[1], header[4], header[5], header[6], header[7],
		header[8], header[9], header[11], header + 12);
	if (st != wu_ok) {
		return st;
	}

	desc->win.w = header[18];
	desc->win.h = header[19];
	desc->win.x = header[20];
	desc->win.y = header[21];
	desc->win.border_w = header[22];
	st = get_window_name(desc);
	if (st != wu_ok) {
		return st;
	}
	st = wuimg_verify(img);
	if (st != wu_ok) {
		return st;
	}

	const size_t stride = strip_length(img->w, desc->bpp, img->align_sh);
	if (stride != header[10]) {
		return wu_invalid_header;
	}

	const uint32_t ncolors = header[17];
	if (img->mode == image_mode_palette) {
		return load_palette(desc, img, ncolors);
	}
	fseek(desc->ifp, XWD_PAL_ENTRY_SIZE*ncolors, SEEK_CUR);
	return wu_ok;
}

enum wu_error xwd_open(struct xwd_desc *desc, FILE *ifp) {
	/* XWD header:
		Offset  Type    Name
		0       u32     HeaderSize  // Size of header (100) + WindowName
		4       u32     Version
		8
	*/
	uint32_t header[2];
	if (fread(header, sizeof(header), 1, ifp)) {
		endian_loop32(header, big_endian, ARRAY_LEN(header));
		if (header[0] >= 100) {
			switch (header[1]) {
			case xwd_x10: return wu_unsupported_feature;
			case xwd_x11: break;
			default: return wu_unknown_file_type;
			}
			*desc = (struct xwd_desc) {
				.ifp = ifp,
				.version = (uint8_t)header[1],
				.win.name.len = header[0] - 100,
			};
			return wu_ok;
		}
		return wu_unknown_file_type;
	}
	return wu_unexpected_eof;
}

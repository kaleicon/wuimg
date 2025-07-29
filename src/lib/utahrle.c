// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2025 kaleido
#include "misc/common.h"
#include "misc/math.h"
#include "raster/fmt.h"
#include "raster/strip.h"
#include "utahrle.h"

/* Format documentation, with important minutiae split amongst three documents:
https://www2.cs.utah.edu/gdc/projects/urt/help/man5/RLE.html
https://www2.cs.utah.edu/gdc/projects/urt/help/urt_descr.html
https://paulbourke.net/dataformats/urt/index.html
*/

enum utah_op {
	utah_skip_lines = 1,
	utah_set_channel = 2,
	utah_skip_pixels = 3,
	utah_byte_data = 5,
	utah_run_data = 6,
	utah_eof = 7,
};

static size_t map_lines(struct utah_desc *desc, struct wuimg *img, size_t y,
size_t lines, size_t stride) {
	const size_t end = zumin(y + lines, img->h);
	if (desc->pal && desc->channels != 1) {
		const uint8_t mask = (uint8_t)((1 << desc->pal_len) - 1);
		while (y < end) {
			uint8_t *row = img->data + y*stride + desc->alpha;
			for (size_t x = 0; x < img->w; ++x) {
				for (size_t z = 0; z < desc->pal_ch; ++z) {
					const size_t i = x*img->channels + z;
					const size_t o = z << desc->pal_len;
					row[i] = desc->pal[
						(o + (row[i] & mask))*2 + 1
					];
				}
			}
			++y;
		}
	} else {
		y = end;
	}
	return y;
}

static size_t decode_rle(struct utah_desc *desc, struct wuimg *img,
const struct wuptr src) {
	size_t s = 0;
	size_t x = 0;
	size_t y = 0;
	uint8_t z = desc->alpha;
	const size_t stride = wuimg_stride(img);
	while (src.len - s >= 2) {
		const enum utah_op op = src.ptr[s];
		uint32_t arg = src.ptr[s+1];
		s += 2;
		const bool thick = op >> 6;
		if (thick) {
			if (src.len - s < 2) {
				break;
			}
			arg = buf_endian16(src.ptr + s, little_endian);
			s += 2;
		}
		switch (op & 0x3f) {
		case utah_skip_lines:
			y = map_lines(desc, img, y, arg, stride);
			x = 0;
			if (y == img->h) {
				return s;
			}
			break;
		case utah_set_channel:
			z = (uint8_t)(arg + desc->alpha);
			if (z >= img->channels) {
				return s;
			}
			x = 0;
			break;
		case utah_skip_pixels:
			if (img->w - x <= arg) {
				return s;
			}
			x += arg;
			break;
		case utah_byte_data:
			arg += 1;
			if (img->w - x < arg || src.len - s < arg + (arg & 1)) {
				return s;
			}
			for (uint32_t i = 0; i < arg; ++i) {
				img->data[y*stride + x*img->channels + z] = src.ptr[s];
				++x;
				++s;
			}
			s += arg & 1;
			break;
		case utah_run_data:
			arg += 1;
			/* For thin op-codes, there'll be a pad byte after the
			 * second arg. For thick codes, the second arg will
			 * be a 16-bit word, but only the low 8 bits will be
			 * used. Point is, we always expect two more bytes, and
			 * the first is always the color to set. */
			if (img->w - x < arg || src.len - s < 2) {
				return s;
			}
			for (uint32_t i = 0; i < arg; ++i) {
				img->data[y*stride + x*img->channels + z] = src.ptr[s];
				++x;
			}
			s += 2;
			break;
		default:
			map_lines(desc, img, y, img->h - y, stride);
			return s;
		}
	}
	return s;
}

struct wu_st utah_decode(struct utah_desc *desc, struct wuimg *img) {
	if (!wuimg_alloc_noverify(img)) {
		return WUERR_HERE(wu_alloc_error);
	}
	const struct wuptr src = mp_avail(&desc->mp, wuimg_size(img)*2);
	return wuerr_partial(decode_rle(desc, img, src), src.len);
}

bool utah_next_comment(const struct utah_desc *desc, struct wuimg *img,
size_t *pos, struct wuptr *key, struct wuptr *val) {
	key->ptr = desc->comm.ptr + *pos;
	key->len = strnlen((const char *)key->ptr, desc->comm.len - *pos);
	*pos += key->len + 1;
	const uint8_t *eq = memchr(key->ptr, '=', key->len);
	if (eq) {
		*val = (struct wuptr) {
			.ptr = eq + 1,
			.len = (size_t)(key->ptr + key->len - (eq + 1)),
		};
		key->len = (size_t)(eq - key->ptr);
		bool image_gamma = wuptr_eq_str(*key, "image_gamma");
		if (image_gamma || wuptr_eq_str(*key, "display_gamma")) {
			char *end;
			double gamma = strtod((const char *)val->ptr, &end);
			if (!*end && gamma != 0) {
				if (image_gamma) {
					gamma = 1.0/gamma;
				}
				color_space_set_gamma(&img->cs, gamma);
			}
		}
	} else {
		*val = (struct wuptr) {
			.ptr = key->ptr + key->len,
			.len = 0,
		};
	}
	return *pos < desc->comm.len;
}

struct wu_st utah_parse(struct utah_desc *desc, struct wuimg *img,
const struct wuptr map) {
	/* Utah RLE header:
		Offset  Type    Name
		0       u8      Magic[2]
		2       s16     X
		4       s16     Y
		6       s16     Width
		8       s16     Height
		10      u8      Flags
		11      u8      Channels
		12      u8      Bitdepth
		13      u8      PalChannels
		14      u8      PalLen
		15
	 * If Flags & 2 == 0, header is followed by BackgroundColor[Channels]
	 * array.

	 * At the next 16-bit boundary:
		0       u16     Palette[PalChannels][1 << PalLen]

	 * At the next 16-bit boundary:
		0       u16     CommentLen
		2       u8      Comments[CommentLen]
	 * Comments are of the form 'key=val\0'

	 * Afterwards comes the compressed raster.
	*/
	*desc = (struct utah_desc) {
		.mp = mp_wuptr(map),
	};
	const uint8_t *hdr = mp_slice(&desc->mp, 15);
	if (!hdr) {
		return WUERR_HERE(wu_unexpected_eof);
	}

	const uint8_t magic[2] = {0x52, 0xcc};
	if (memcmp(hdr, magic, sizeof(magic))) {
		return WUERR_HERE(wu_invalid_signature);
	}

	desc->x = (int16_t)buf_endian16(hdr + 2, little_endian);
	desc->y = (int16_t)buf_endian16(hdr + 4, little_endian);
	desc->clear = hdr[10] & 0x1;
	desc->alpha = hdr[10] & 0x4;
	const bool comments = hdr[10] & 0x8;
	const bool has_bg = !(hdr[10] & 0x2);
	desc->channels = hdr[11];
	desc->pal_ch = hdr[13];
	desc->pal_len = hdr[14];
	img->channels = desc->channels;
	img->bitdepth = hdr[12];
	img->mirror = true;

	const int16_t w = (int16_t)buf_endian16(hdr + 6, little_endian);
	const int16_t h = (int16_t)buf_endian16(hdr + 8, little_endian);
	const char *err = NULL;
	if (w < 1 || h < 1) {
		err = "negative dimensions";
	} else if (desc->channels == 255) {
		err = "255 channels not allowed";
	} else if (img->bitdepth != 8) {
		err = "bitdepth != 8";
	} else if (desc->pal_len > img->bitdepth) {
		err = "excessive palette size";
	} else if (desc->channels != 1 && desc->pal_ch
	&& desc->channels != desc->pal_ch) {
		err = "channel mismatch for raster and palette";
	}
	if (err) {
		return wuerr(wu_invalid_header, err);
	} else if (!desc->channels && !desc->alpha) {
		return WUERR_HERE(wu_no_image_data);
	}

	img->w = (size_t)w;
	img->h = (size_t)h;

	const size_t bg_len = has_bg ? desc->channels : 0;
	hdr = mp_slice(&desc->mp, bg_len + !(bg_len & 1));
	desc->bg = has_bg ? hdr : NULL;
	if (!hdr) {
		return WUERR_HERE(wu_unexpected_eof);
	}

	if (desc->pal_ch) {
		if (desc->alpha) {
			return wuerr(wu_uncertain_validity,
				"paletted image with separate alpha channel");
		}
		const size_t pal_len = desc->pal_ch*2u << desc->pal_len;
		desc->pal = mp_slice(&desc->mp, pal_len);
		if (!desc->pal) {
			return WUERR_HERE(wu_unexpected_eof);
		}
		if (desc->channels == 1 && desc->pal_ch <= 3) {
			struct palette *pal = wuimg_palette_init(img);
			if (!pal) {
				return WUERR_HERE(wu_alloc_error);
			}

			uint8_t *dst = (uint8_t *)pal->color;
			for (size_t x = 0; x < (1u << desc->pal_len); ++x) {
				for (size_t z = 0; z < desc->pal_ch; ++z) {
					size_t d = x*sizeof(*pal->color) + z;
					size_t s = ((z << desc->pal_len) + x)*2 + 1;
					dst[d] = desc->pal[s];
				}
			}
			for (size_t i = 0; i < ARRAY_LEN(pal->color); ++i) {
				pal->color[i].a = 0xff;
			}
			img->layout = img->channels == 1
				? (uint8_t)pix_layout_pack(0, 0, 0, 3)
				: pix_rgba;
			// TODO
			err = "single channel paletted images untested, display may be wrong";
		}
	} else if (desc->alpha) {
		/* Alpha is hardcoded in the stream as channel 255 (-1), but
		 * since we can swizzle channels at will, we add 1 to all
		 * indexes so that it becomes channel 0. */
		img->channels += desc->alpha;
		switch (img->channels) {
		case 1: img->layout = pix_gray; break;
		case 2: img->layout = pix_layout_pack(1, 1, 1, 0); break;
		default: img->layout = pix_argb; break;
		}
	}

	if (comments) {
		uint16_t comm_len;
		hdr = mp_slice(&desc->mp, sizeof(comm_len));
		if (!hdr) {
			return WUERR_HERE(wu_unexpected_eof);
		}
		comm_len = buf_endian16(hdr, little_endian);
		hdr = mp_slice(&desc->mp, comm_len + (comm_len & 1));
		desc->comm = (struct wuptr) {
			.len = comm_len,
			.ptr = hdr,
		};
		if (!hdr) {
			return WUERR_HERE(wu_unexpected_eof);
		}
	}

	struct wu_st st = wuimg_verify_st(img);
	if (wu_isok(st)) {
		st.msg = err;
	}
	return st;
}

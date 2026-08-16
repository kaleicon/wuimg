// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2025 kaleido
#include <stdlib.h>
#include <string.h>

#include "misc/common.h"
#include "misc/math.h"
#include "misc/mem.h"
#include "raster/fmt.h"
#include "lib/elecbyte.h"

/* Commented FNTv1 and SFFv1 specs (in doc/formats.txt):
https://network.mugenguild.com/winane/software/sffextract-0.93-src.zip

 * SFFv2 spec:
https://web.archive.org/web/20150510210608/http://elecbyte.com:80/wiki/index.php/SFFv2
https://web.archive.org/web/20141230125932/http://elecbyte.com/wiki/index.php/LZ5

 * This implementation is a bit messy. I blame PCX. PCX corrupts everything
 * it touches.
*/

size_t eb_print_version(char str[static 16], const uint8_t version[static 4]) {
	return (size_t)imax(0, snprintf(str, 16, "%hhu.%hhu.%hhu.%hhu",
		version[3], version[2], version[1], version[0]));
}

const char * eb_sff2_format_str(const enum eb_sff2_format fmt) {
	switch (fmt) {
	case eb_sff2_raw: return "Raw";
	case eb_sff2_rle5: return "RLE5";
	case eb_sff2_lz5: return "LZ5";
	case eb_sff2_rle8: return "RLE8";
	case eb_sff2_png8: return "PNG8";
	case eb_sff2_png24: return "PNG24";
	case eb_sff2_png32: return "PNG32";
	}
	return "???";
}

uint8_t eb_sff_get_version(const struct eb_sff_desc *desc) {
	return desc->version[3];
}

enum eb_sff_decoder eb_sff_get_decoder(const struct eb_sff_desc *desc,
const struct eb_sff_sub *sub) {
	if (eb_sff_get_version(desc) == 2) {
		switch (sub->u.v2.fmt) {
		case eb_sff2_png8:
		case eb_sff2_png24:
		case eb_sff2_png32:
			return eb_sff_png;
		default: break;
		}
		return eb_sff_sff;
	}
	return eb_sff_pcx;
}

void eb_sff_cleanup(struct eb_sff_desc *desc) {
	if (eb_sff_get_version(desc) == 2) {
		const struct eb_sff2 *v2 = &desc->u.v2;
		if (v2->pals) {
			for (uint32_t i = 0; i < v2->pal_nr; ++i) {
				palette_unref(v2->pals[i]);
			}
			free(v2->pals);
		}
	}
}

void eb_sff_touchup(const struct eb_sff_desc *desc,
const struct eb_sff_sub *sub, struct wuimg *dst, struct wuimg *first) {
	if (eb_sff_get_version(desc) == 1 && sub->u.v1.shared_pal && dst != first) {
		if (first->mode == image_mode_palette) {
			if (dst->mode == image_mode_palette) {
				palette_unref(dst->u.palette);
				dst->mode = image_mode_raw;
			}
			dst->layout = pix_rgba;
			wuimg_palette_set(dst, palette_ref(first->u.palette));
		}
	}
}

#define MAX_READ (1 + 8*3)
static size_t lz5_decode(uint8_t *restrict dst, const size_t dst_len,
struct wuptr src) {
	size_t d = 0;
	size_t s = 0;
	unsigned recycle = 0;
	unsigned rec_cnt = 0;
	uint8_t end[MAX_READ*2];
	for (;;) {
		if (s + MAX_READ > src.len) {
			if (src.ptr == end) {
				break;
			}
			src.ptr = mem_bufswitch(src.ptr, &s, &src.len, end,
				sizeof(end));
		}
		uint8_t flags = src.ptr[s];
		++s;
		for (int i = 0; i < 8; ++i, flags >>= 1) {
			uint8_t first = src.ptr[s];
			++s;
			size_t len;
			if (flags & 0x1) {
				size_t off;
				len = first & 0x3f;
				if (len) { // Short LZ
					++len;
					recycle |= (first & 0xc0u) >> (rec_cnt % 4)*2;
					++rec_cnt;
					if (rec_cnt % 4 == 0) {
						off = recycle;
						recycle = 0;
					} else {
						off = src.ptr[s];
						++s;
					}
				} else { // Long LZ
					off = (size_t)first << 2 | src.ptr[s];
					len = src.ptr[s+1] + 3;
					s += 2;
				}
				++off;
				if (dst_len - d < len) {
					return d;
				}
				memrepeat_or_zero(dst, d, off, len);
			} else {
				len = first >> 5;
				if (!len) {
					len = src.ptr[s] + 8;
					++s;
				}
				if (dst_len - d < len) {
					return d;
				}
				memset(dst + d, first & 0x1f, len);
			}
			d += len;
		}
	}
	return d;
}

static size_t rle5_decode(uint8_t *restrict dst, const size_t dst_len,
const struct wuptr src) {
	size_t s = 0;
	size_t d = 0;
	while (src.len - s >= 2) {
		const size_t setlen = src.ptr[s] + 1;
		const uint8_t byte = src.ptr[s+1];
		s += 2;
		uint8_t color = 0;
		if (byte & 0x80) {
			if (src.len - s == 0) {
				break;
			}
			color = src.ptr[s];
			++s;
		}
		if (dst_len - d < setlen) {
			break;
		}
		memset(dst + d, color, setlen);

		const uint8_t datalen = byte & 0x7f;
		if (src.len - s < datalen) {
			break;
		}
		for (int i = 0; i < datalen; ++i) {
			const uint8_t c = src.ptr[s];
			++s;
			const uint8_t len = (c >> 5) + 1;
			if (dst_len - d < len) {
				return d;
			}
			memset(dst + d, c & 0x1f, len);
		}
	}
	return d;
}

static size_t rle8_decode(uint8_t *restrict dst, const size_t dst_len,
const struct wuptr src) {
	size_t d = 0;
	size_t s = 0;
	while (s < src.len) {
		size_t len = 1;
		uint8_t c = src.ptr[s];
		++s;
		if (c >> 6 == 1) {
			if (s >= src.len) {
				break;
			}
			len = (c & 0x3f);
			c = src.ptr[s];
			++s;
		}
		if (dst_len - d < len) {
			break;
		}
		memset(dst + d, c, len);
		d += len;
	}
	return d;
}

struct wu_st eb_sff2_dec(const struct eb_sff_sub *sub, struct wuimg *img) {
	size_t w = 0;
	const struct wuptr data = sub->data;
	const size_t img_size = wuimg_size(img);
	switch (sub->u.v2.fmt) {
	case eb_sff2_raw:
		w = zumin(img_size, data.len);
		memcpy(img->data, data.ptr, w);
		break;
	case eb_sff2_rle8:
		w = rle8_decode(img->data, img_size, data);
		break;
	case eb_sff2_rle5:
		w = rle5_decode(img->data, img_size, data);
		break;
	case eb_sff2_lz5:
		w = lz5_decode(img->data, img_size, data);
		break;
	default: break;
	}
	return wuerr_partial(w, img_size);
}

struct wu_st eb_sff2_get_dims(const struct eb_sff_sub *sub,
struct wuimg *img) {
	const struct eb_sff2_sub *sub2 = &sub->u.v2;
	img->w = sub2->w;
	img->h = sub2->h;
	img->channels = sub2->depth == 5 ? 1 : sub2->depth / 8;
	img->bitdepth = 8;
	if (img->channels == 1) {
		img->alpha = alpha_ignore;
		wuimg_palette_set(img, palette_ref(sub2->pal));
	}
	return WU_OK;
}

static struct wuptr get_comment(const uint8_t *data, const size_t len) {
	return wuptr_trim_end(wuptr_mem(data, len), 0);
}

static struct wu_st load_v2_pal(struct eb_sff_desc *desc,
struct eb_sff_sub *sub, uint16_t i) {
	/* Palette struct:
		Offset  Type    Name
		0       u16     Group
		2       u16     Item
		4       u16     Colors
		6       u16     LinkedIndex
		8       u32     LDataOffset
		12      u32     Length
		16
	 * If Length == 0, load the palette at LinkedIndex.
	 * Palette data is in RGBX order.
	*/

	const size_t hdr_size = 16;
	struct eb_sff2 *v2 = &desc->u.v2;
	for (int tries = 0; tries < 2; ++tries) {
		if (i >= v2->pal_nr) {
			return wuerr(wu_invalid_header,
				"palette number out of bounds");
		}

		struct palette *pal = v2->pals[i];
		if (pal) {
			sub->u.v2.pal = pal;
			return WU_OK;
		}

		const uint8_t *hdr = mp_slice_at(&desc->mp,
			v2->pal_off + i*hdr_size, hdr_size);
		if (!hdr) {
			return WUERR_HERE(wu_unexpected_eof);
		}

		uint32_t off = buf_endian32l(hdr + 8);
		size_t len = zumin(buf_endian32l(hdr + 12),
			(1 << sub->u.v2.depth) * 4);
		if (len) {
			struct mparser data = mp_wuptr(v2->ldata);
			const uint8_t *color = mp_slice_at(&data, off, len);
			if (color) {
				pal = palette_new();
				if (!pal) {
					return WUERR_HERE(wu_alloc_error);
				}
				v2->pals[i] = pal;
				sub->u.v2.pal = pal;
				memcpy(pal->color, color, len);
				return WU_OK;
			}
			return WUERR_HERE(wu_unexpected_eof);
		}
		i = buf_endian16l(hdr + 6);
	}
	return wuerr(wu_invalid_header, "followed too many linked palettes");
}

static bool valid_fmt_depth(const enum eb_sff2_format fmt, const uint8_t depth) {
	switch (fmt) {
	case eb_sff2_raw:
		switch (depth) {
		case 5: case 8: case 24:
		case 32: // TODO: samples needed
			return true;
		}
		return false;
	case eb_sff2_rle5:
	case eb_sff2_lz5:
		return depth == 5;
	case eb_sff2_rle8:
	case eb_sff2_png8:
		return depth == 8;
	case eb_sff2_png24: return depth == 24;
	case eb_sff2_png32: return depth == 32;
	}
	return false;
}

static struct wu_st sff2_next(struct eb_sff_desc *desc, struct eb_sff_sub *sub) {
	/* SFFv2 Image header:
		Offset  Type    Name
		0       u16     Group
		2       u16     Item
		4       u16     Width
		6       u16     Height
		8       u16     X
		10      u16     Y
		12      u16     LinkedItemNum
		14      u8      Format
		15      u8      Depth
		16      u32     Offset
		20      u32     Len
		24      u16     PaletteIdx
		26      u16     Flags
		28
	 * If Flags & 1, Offset is relative to TData, otherwise LData.
	 * The first 4 bytes of Data is the uncompressed size, so that must be
	 * skipped.
	*/
	struct eb_sff2 *v2 = &desc->u.v2;
	if (v2->cur >= desc->images) {
		return WU_NO_CHANGE;
	}
	mp_seek_set(&desc->mp, v2->image_off + v2->cur * 28);
	++v2->cur;
	const uint8_t *hdr = mp_slice(&desc->mp, 28);
	if (hdr) {
		const enum eb_sff2_format fmt = hdr[14];
		uint8_t depth = hdr[15];
		const uint32_t len = buf_endian32l(hdr + 20);
		const uint16_t flags = buf_endian16l(hdr + 26);

		if (!len) {
			return WU_NO_CHANGE;
		}

		if (!valid_fmt_depth(fmt, depth) || flags > 1) {
			return wuerr(wu_invalid_header,
				"bad depth or unknown flags set");
		}

		struct mparser data = mp_wuptr(flags ? v2->tdata : v2->ldata);
		*sub = (struct eb_sff_sub) {
			.u.v2 = {
				.group = buf_endian16l(hdr),
				.item = buf_endian16l(hdr + 2),
				.w = buf_endian16l(hdr + 4),
				.h = buf_endian16l(hdr + 6),
				.x = buf_endian16l(hdr + 8),
				.y = buf_endian16l(hdr + 10),
				.fmt = fmt,
				.depth = depth,
			},
			.data = mp_avail_at(&data,
				buf_endian32l(hdr + 16),
				buf_endian32l(hdr + 20)),
		};
		if (sub->data.len > 4) {
			uint16_t paln = buf_endian16l(hdr + 24);
			sub->data.len -= 4;
			sub->data.ptr += 4;
			return (depth > 8)
				? WU_OK
				: load_v2_pal(desc, sub, paln);
		}
	}
	return WUERR_HERE(wu_unexpected_eof);
}

static struct wu_st sff1_next(struct eb_sff_desc *desc, struct eb_sff_sub *sub) {
	/* SFFv1 Image header:
		Offset  Type    Name
		0       u32     NextImageOffset
		4       u32     FileLength
		8       s16     X
		10      s16     Y
		12      s16     GroupNum
		14      s16     ImageNum
		16      s16     PrevSprite
		18      u8      UsePrevPalette
		19      u8      Comment[13]
		32      u8      PCXData[FileLength]
	*/
	if (desc->mp.pos == 0) {
		return WU_NO_CHANGE;
	}

	const uint8_t *subhdr = mp_slice(&desc->mp, 32);
	if (subhdr) {
		const uint32_t next = buf_endian32l(subhdr);
		const uint32_t len = buf_endian32l(subhdr + 4);
		if (subhdr[18] <= 1) {
			*sub = (struct eb_sff_sub) {
				.u.v1 = {
					.x = (int16_t)buf_endian16l(subhdr + 8),
					.y = (int16_t)buf_endian16l(subhdr + 10),
					.group = (int16_t)buf_endian16l(subhdr + 12),
					.image = (int16_t)buf_endian16l(subhdr + 14),
					.prev = (int16_t)buf_endian16l(subhdr + 16),
					.shared_pal = subhdr[18],
					.comm = get_comment(subhdr + 19, 32 - 19),
				},
				.data = mp_avail(&desc->mp, len),
			};
			mp_seek_set(&desc->mp, next);
			return len ? WU_OK : WU_NO_CHANGE;
		}
		return wuerr(wu_invalid_header, "use_prev_palette neither 0 nor 1");
	}
	return WUERR_HERE(wu_unexpected_eof);
}

struct wu_st eb_sff_next(struct eb_sff_desc *desc, struct eb_sff_sub *sub) {
	switch (eb_sff_get_version(desc)) {
	case 1: return sff1_next(desc, sub);
	case 2: return sff2_next(desc, sub);
	}
	return wuerr(wu_invalid_params,
		"tried to iterate over unknown sff version");
}

static struct wu_st sff2_parse(struct eb_sff_desc *desc) {
	/* ElecbyteSpr v2 header (after signature and version):
		Offset  Type    Name
		0       u8      Reserved[8]
		8       u8      CompatLow3
		9       u8      CompatLow2
		10      u8      CompatLow1
		11      u8      CompatHigh
		12      u8      Reserved[8]
		20      u32     SpriteOffset
		24      u32     Sprites
		28      u32     PaletteOffset
		32      u32     Palettes
		36      u32     LDataOffset
		40      u32     LDataLen
		44      u32     TDataOffset
		48      u32     TDataLen
		52      u8      Reserved[8]
		60      u8      Comment[436]
		496
	 * In MUGEN, image data from LData is copied to memory and decoded as
	 * needed, while TData is decoded inmediately. This of course makes no
	 * difference to us.
	*/
	const uint8_t *hdr = mp_slice(&desc->mp, 496);
	if (hdr) {
		desc->images = buf_endian32l(hdr + 24);
		desc->comm = get_comment(hdr + 60, 496 - 60);

		struct eb_sff2 *v2 = &desc->u.v2;
		uint32_t pal_nr = buf_endian32l(hdr + 32);
		// Images can't address palettes higher than this
		if (pal_nr > 0x10000) {
			return wuerr(wu_invalid_header, "palette_nr > 0x10000");
		}
		v2->image_off = buf_endian32l(hdr + 20);
		v2->pal_off = buf_endian32l(hdr + 28);
		v2->pal_nr = pal_nr;
		v2->ldata = mp_avail_at(&desc->mp,
			buf_endian32l(hdr + 36),
			buf_endian32l(hdr + 40));
		v2->tdata = mp_avail_at(&desc->mp,
			buf_endian32l(hdr + 44),
			buf_endian32l(hdr + 48));
		if (pal_nr) {
			v2->pals = small_calloc(pal_nr, sizeof(*v2->pals));
			return v2->pals
				? WU_OK : WUERR_HERE(wu_alloc_error);
		}
		return WU_OK;
	}
	return WUERR_HERE(wu_unexpected_eof);
}

static struct wu_st sff1_parse(struct eb_sff_desc *desc) {
	/* ElecbyteSpr v1 header (after signature and version):
		Offset  Type    Name
		0       u32     Groups
		4       u32     Images
		8       u32     FirstImageOffset
		12      u32     HeaderSize       // 32 or 512
		16      u8      PaletteType
		17      u8      Padding[3]
		20      u8      Comment[476]
		496
	*/
	const uint8_t *hdr = mp_slice(&desc->mp, 496);
	if (!hdr) {
		return WUERR_HERE(wu_unexpected_eof);
	}
	const uint32_t hdr_size = buf_endian32l(hdr + 12);
	if (hdr_size != 32 && hdr_size != 512) {
		return wuerr(wu_invalid_header,
			"header size neither 32 nor 512");
	} else if (hdr[16] >= 2) {
		return wuerr(wu_invalid_header,
			"palette type neither 0 nor 1");
	}
	desc->images = buf_endian32l(hdr + 4);
	desc->comm = get_comment(hdr + 20, 496 - 20);

	struct eb_sff1 *v1 = &desc->u.v1;
	v1->groups = buf_endian32l(hdr);
	v1->first = buf_endian32l(hdr + 8);
	v1->shared_pal = hdr[16];
	mp_seek_set(&desc->mp, v1->first);
	return WU_OK;
}

struct wu_st eb_sff_parse(struct eb_sff_desc *desc, const struct wuptr mem) {
	/* ElecbyteSpr signature:
		Offset  Type    Name
		0       u8      Magic[12]
		12      u8      VersionLow3
		13      u8      VersionLow2
		14      u8      VersionLow1
		15      u8      VersionHigh
		16
	*/
	*desc = (struct eb_sff_desc) {.mp = mp_wuptr(mem)};
	const uint8_t sig[12] = "ElecbyteSpr\0";
	const uint8_t *hdr = mp_slice(&desc->mp, 16);
	if (hdr) {
		if (!memcmp(hdr, sig, sizeof(sig))) {
			memcpy(desc->version, hdr + 12, sizeof(desc->version));
			switch (eb_sff_get_version(desc)) {
			case 1: return sff1_parse(desc);
			case 2: return sff2_parse(desc);
			}
			return wuerr(wu_invalid_header,
				"sff version is neither 1 nor 2");
		}
		return WUERR_HERE(wu_invalid_signature);
	}
	return WUERR_HERE(wu_unexpected_eof);
}

static struct wuptr get_loc(struct mparser *mp, const uint8_t *hdr) {
	return mp_avail_at(mp,
		buf_endian32(hdr, little_endian),
		buf_endian32(hdr + 4, little_endian));
}

struct wu_st eb_fnt_parse(struct eb_fnt_desc *desc, const struct wuptr mem) {
	/* ElecbyteFnt header:
		Offset  Type    Name
		0       u8      Magic[12]
		12      u8      VersionLow3
		13      u8      VersionLow2
		14      u8      VersionLow1
		15      u8      VersionHigh
		16      u32     PCXOffset
		20      u32     PCXLength
		24      u32     TextOffset
		28      u32     TextLength
		32      u8      Comment[32]
		64
	*/
	struct mparser mp = mp_wuptr(mem);
	const uint8_t sig[12] = {'E','l','e','c','b','y','t','e','F','n','t',0};
	const uint8_t *hdr = mp_slice(&mp, 64);
	if (!hdr) {
		return WUERR_HERE(wu_unexpected_eof);
	} else if (memcmp(hdr, sig, sizeof(sig))) {
		return WUERR_HERE(wu_invalid_signature);
	}
	memcpy(desc->version, hdr + 12, sizeof(desc->version));
	desc->pcx = get_loc(&mp, hdr + 16);
	desc->text = get_loc(&mp, hdr + 24);
	desc->comment = get_comment(hdr + 32, 32);
	return (desc->pcx.len)
		? WU_OK
		: WUERR_HERE(wu_unexpected_eof);
}

// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2025 kaleido
#include <string.h>

#include "lib/eclipse.h"
#include "misc/endian.h"

static const uint32_t TSIZE = 0x100;

struct wu_st eclipse_load(struct eclipse_desc *desc, struct wuimg *img) {
	size_t r = 0;
	if (!wuimg_alloc_noverify(img)) {
		return WUERR_HERE(wu_alloc_error);
	}

	fseek(desc->ifp, 0x1000, SEEK_SET);
	const uint32_t tw = desc->w >> 8;
	const uint32_t th = desc->h >> 8;
	const size_t stride = (size_t)desc->w * img->channels;
	const size_t tstride = TSIZE * img->channels;
	uint8_t *dst = img->data;
	for (uint32_t ty = 0; ty < th; ++ty) {
		for (uint32_t tx = 0; tx < tw; ++tx) {
			uint8_t *d = dst + ty*TSIZE*stride + tx*tstride;
			const uint32_t lim = (ty + 1 < th)
				? TSIZE : img->h & 0xff;
			for (uint32_t y = 0; y < lim; ++y) {
				r += fread(d + y*stride, 1, tstride, desc->ifp);
			}
			fseek(desc->ifp, (long)((TSIZE - lim)*tstride), SEEK_CUR);
		}
	}
	return wuerr_partial(r, wuimg_size(img));
}

struct wu_st eclipse_init(struct eclipse_desc *desc, struct wuimg *img,
FILE *ifp) {
	/* Eclipse TILE header:
		Offset  Type    Name
		0       u16     ID?        // 07 28
		2       u16     Version?   // 0 or 1
		4       u32     Width
		8       u32     Height
		12      u32     Colorspace
		16      u8      Name1[32]  // "Eclipse" padded with zeros
		48      u8      Name2[32]  // A short zero terminated string
		80      u32     ???        // 3 or 0 for RGB, 4 for CMYK?
		84      u32     Num1       // 40 27 9f 3e
		88      u32     Num2       // 7c f9 f3 e8
		92      u32     Num1       // Same as prev Num1
		96      u32     Num2       // Same as prev Num2
		100     u8      ???[16]
		116     u32     CeilWidth
		120     u32     CeilHeight
		124     u8      Padding[0x1000 - 124]
		0x1000
	 * This is followed by an ABGR image split into 256x256 tiles,
	 * with padding included for edge tiles. Image is stored bottom-up
	 * and tiles are ordered bottom-up left-to-right.

	 * The related Eclipse Proxy format is implemented in auto.c
	*/
	desc->ifp = ifp;
	uint32_t hdr[31];
	if (fread(hdr, sizeof(hdr), 1, ifp)) {
		const uint32_t sig = endian32b(hdr[0]);
		desc->version = sig & 0xffff;
		if (sig >> 16 != 0x0728) {
			return WUERR_HERE(wu_unknown_file_type);
		} else if (desc->version > 1) {
			return wuerr(wu_uncertain_validity,
				"eclipse tile version > 1");
		}
		memcpy(desc->software, hdr + 4, sizeof(desc->software));
		memcpy(desc->revision, hdr + 12, sizeof(desc->revision));
		img->w = endian32b(hdr[1]);
		img->h = endian32b(hdr[2]);
		desc->w = (img->w + 0xff) & ~0xffu;
		desc->h = (img->h + 0xff) & ~0xffu;
		img->channels = 4;
		img->bitdepth = 8;
		img->layout = pix_abgr;
		img->mirror = true;
		/* Set alignment such that we don't have to worry about
		 * right edge tiles.
		 * Equivalent to log2(0x100 * sizeof(uint32_t)) */
		img->align_sh = 10;

		const uint32_t colorspace = endian32b(hdr[3]);
		switch (colorspace) {
		case eclipse_rgb:
			img->alpha = alpha_ignore;
			break;
		case eclipse_cmyk:
			img->alpha = alpha_key;
			img->cs.invert = true;
			img->cs.invert_alpha = true;
			break;
		case eclipse_alpha:
			img->channels = 1;
			img->layout = pix_gray;
			break;
		default:
			return wuerr(wu_samples_wanted,
				"unknown colorspace");
		}
		desc->colorspace = colorspace;
		return wuerr(wuimg_verify(img), NULL);
	}
	return WUERR_HERE(wu_unexpected_eof);
}

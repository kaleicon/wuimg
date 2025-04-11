// SPDX-License-Identifier: 0BSD
/* Eclipse TILE */
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
	uint32_t *dst = (uint32_t *)img->data;
	for (uint32_t ty = 0; ty < th; ++ty) {
		for (uint32_t tx = 0; tx < tw; ++tx) {
			uint32_t *d = dst + ty*TSIZE*desc->w + tx*TSIZE;
			const uint32_t lim = (ty + 1 < th)
				? TSIZE : img->h & 0xff;
			for (uint32_t y = 0; y < lim; ++y) {
				r += fread(d + y*desc->w, 4, TSIZE, desc->ifp);
			}
			fseek(desc->ifp, (long)(TSIZE - lim)*0x400, SEEK_CUR);
		}
	}
	return wuerr(r ? wu_ok : wu_unexpected_eof, NULL);
}

struct wu_st eclipse_init(struct eclipse_desc *desc, struct wuimg *img,
FILE *ifp) {
	/* Eclipse TILE header:
		Offset  Type    Name
		0       u32     ID?        // 07 28 00 00
		4       u32     Width
		8       u32     Height
		12      u32     Colorspace // 0 (RGB) or 1 (CMYK)
		16      u8      Name1[32]  // "Eclipse" padded with zeros
		48      u8      Name2[32]  // A short zero terminated string
		80      u32     Channels   // 3 for RGB, 4 for CMYK
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
		const uint8_t sig[4] = {0x07, 0x28, 0, 0};
		if (!memcmp(hdr, sig, sizeof(sig))) {
			memcpy(desc->software, hdr + 4, sizeof(desc->software));
			memcpy(desc->revision, hdr + 12, sizeof(desc->revision));
			img->w = endian32(hdr[1], big_endian);
			img->h = endian32(hdr[2], big_endian);
			desc->w = (img->w + 0xff) & ~0xffu;
			desc->h = (img->h + 0xff) & ~0xffu;
			const uint32_t colorspace = endian32(hdr[3], big_endian);
			const uint32_t comps = endian32(hdr[20], big_endian);
			switch (colorspace) {
			case eclipse_rgb:
				// solarclips has comps == 0
				if (comps != 0 && comps != 3) {
					return wuerr(wu_samples_wanted,
						"channels != 3 for RGB image");
				}
				img->alpha = alpha_ignore;
				img->attr = pix_normal;
				break;
			case eclipse_cmyk:
				if (comps != 4) {
					return wuerr(wu_samples_wanted,
						"channels != 4 for CMYK image");
				}
				img->attr = pix_inverted;
				img->alpha = alpha_key;
				break;
			default:
				return wuerr(wu_samples_wanted,
					"unknown colorspace");
			}
			desc->colorspace = colorspace;
			img->channels = 4;
			img->bitdepth = 8;
			img->layout = pix_abgr;
			img->mirror = true;
			/* Set alignment so we don't have to worry about right
			 * edge tiles. */
			img->align_sh = 10; // log2(0x100 * sizeof(uint32_t))
			return wuerr(wuimg_verify(img), NULL);
		}
		return WUERR_HERE(wu_unknown_file_type);
	}
	return WUERR_HERE(wu_unexpected_eof);
}

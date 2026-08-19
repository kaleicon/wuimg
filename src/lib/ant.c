// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2025 kaleido
#include <string.h>

#include "lib/ant.h"
#include "misc/math.h"
#include "raster/fmt.h"

// Studio e.Go ANT Image. Used for outlines.

struct wu_st ant_decode(const struct ant_desc *desc, struct wuimg *img) {
	size_t dst_len = wuimg_size(img);
	size_t src_len = zumin(desc->size, dst_len);
	uint32_t *dst = (uint32_t *)img->data;
	uint8_t *src = img->data + dst_len - src_len;
	dst_len /= sizeof(*dst);
	src_len = fread(src, 1, src_len, desc->ifp);
	size_t d = 0;
	size_t s = 0;
	while (src_len - s >= 2 && d < dst_len) {
		const uint8_t alpha = src[s];
		if (alpha) {
			if (src_len - s < sizeof(*dst)) {
				break;
			}
			memmove(dst + d, src + s, sizeof(*dst));
			s += sizeof(*dst);
			++d;
		} else {
			const uint8_t count = src[s+1];
			if (dst_len - d < count) {
				break;
			}
			memset(dst + d, 0, sizeof(*dst) * count);
			s += 2;
			d += count;
		}
	}
	if (src_len && d < dst_len) {
		memset(dst + d, 0, (dst_len - d) * sizeof(*dst));
	}
	return wuerr_partial(d, dst_len);
}

struct wu_st ant_init(struct ant_desc *desc, struct wuimg *img, FILE *ifp) {
	/* ANT header:
		Offset  Type    Name
		0       u8      Magic[4]
		4       u32     ???       // always 0x10?
		8       u32     FileSize
		12      u32     Width
		16      u32     Height
		20      u32     ???       // always 0?
		24      u8      Data[FileSize - 24]
		FileSize
	*/
	const uint8_t magic[4] = {'A', 'N', 'T', 'I'};
	uint32_t hdr[6];
	if (!fread(hdr, sizeof(hdr), 1, ifp)) {
		return WUERR_HERE(wu_unexpected_eof);
	} else if (memcmp(hdr, magic, sizeof(magic))) {
		return WUERR_HERE(wu_invalid_signature);
	} else if (endian32l(hdr[1]) != 0x10) {
		return wuerr(wu_invalid_header, "mystery field 1 != 0x10");
	}
	*desc = (struct ant_desc) {
		.ifp = ifp,
		.size = endian32l(hdr[2]),
	};
	if (desc->size < sizeof(hdr)) {
		return wuerr(wu_invalid_header, "bad stream size");
	}
	desc->size -= (uint32_t)sizeof(hdr);
	img->w = endian32l(hdr[3]);
	img->h = endian32l(hdr[4]);
	img->channels = 4;
	img->bitdepth = 8;
	img->layout = pix_abgr;
	return WU_OK;
}

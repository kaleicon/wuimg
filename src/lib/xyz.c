// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2022 kaleido
#include <stdlib.h>
#include <string.h>

#include "misc/decomp.h"
#include "raster/fmt.h"
#include "xyz.h"

struct wu_st xyz_decode(struct wuimg *img, struct wuptr src) {
	/* Deflate stream contains an RGB24 palette, followed by index data.
	 * Although zlib allows us to pause decoding to switch output buffers,
	 * for simplicity, we decompress to a single buffer, expand the
	 * palette to RGBA in place, and make img->data point after
	 * img->u.palette while setting the borrowed bit to true. The image
	 * will thus be freed when the palette is. */
	src.ptr += 8;
	src.len -= 8;
	struct palette *pal;
	const size_t dst_len = sizeof(*pal) + wuimg_size(img);
	const size_t entries = 256;
	const size_t write_offset = sizeof(*pal) - entries*3;
	const size_t uncmp_len = dst_len - write_offset;

	uint8_t *dst = malloc(dst_len);
	if (dst) {
		uint8_t *uncmp = dst + write_offset;
		const size_t uncmpd = decomp_deflate(uncmp, uncmp_len,
			src.ptr, src.len);
		if (uncmpd > entries*3) {
			pal = (struct palette *)dst;
			pal->refs = 0;
			palette_from_rgb8(pal, uncmp, entries);
			wuimg_palette_set(img, pal);
			img->data = dst + sizeof(*pal);
			img->borrowed = true;
			memset(dst + uncmpd, 0, uncmp_len - uncmpd);
			return wuerr_partial((size_t)uncmpd, uncmp_len);
		}
		free(dst);
		return wuerr(wu_unexpected_eof,
			"deflate stream ended before image data");
	}
	return WUERR_HERE(wu_alloc_error);
}

struct wu_st xyz_parse(struct wuimg *img, const struct wuptr mem) {
	/* XYZ header:
		Offset  Type    Name
		0       u8      Magic[4]
		4       u16     Width
		6       u16     Height
		8       u8      DeflateStream[]
	*/
	const uint8_t magic[] = {'X', 'Y', 'Z', '1'};
	if (mem.len <= 8) {
		return WUERR_HERE(wu_unexpected_eof);
	} else if (memcmp(mem.ptr, magic, sizeof(magic))) {
		return WUERR_HERE(wu_invalid_signature);
	}
	img->w = buf_endian16l(mem.ptr + 4);
	img->h = buf_endian16l(mem.ptr + 6);
	img->channels = 1;
	img->bitdepth = 8;
	img->layout = pix_rgba;
	return wuimg_verify_st(img);
}

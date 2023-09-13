// SPDX-License-Identifier: 0BSD
#include <stdlib.h>

#include <zlib.h>

#include "misc/common.h"
#include "raster/fmt.h"
#include "xyz.h"

size_t xyz_decode(const struct mp_parser *mp, struct wuimg *img) {
	/* XYZ is simply a deflate stream, containing an RGB palette and the
	 * index data.
	 * Although zlib allows us to pause decoding to switch output buffers,
	 * for simplicity, we decompress to a single buffer, expand the
	 * palette to RGBA in place, and make img->data point after
	 * img->u.palette while setting the borrowed bit to true. The image
	 * will be freed when the palette is. */
	const struct wuptr src = mp_remaining_at(mp, mp->pos, SIZE_MAX);
	if (src.len) {
		struct raster_pal *pal;
		const size_t dst_len = sizeof(*pal) + wuimg_size(img);
		uint8_t *dst = malloc(dst_len);
		if (dst) {
			const size_t pal_items = ARRAY_LEN(pal->color);
			uint8_t *uncmp = dst + pal_items;
			uLong uncmp_len = (uLong)(dst_len - pal_items);
			uncompress(uncmp, &uncmp_len, src.ptr, (uLong)src.len);
			if (uncmp_len > pal_items*3) {
				pal = (struct raster_pal *)dst;
				raster_pal_from_rgb8(pal, uncmp, pal_items);
				wuimg_palette_set(img, pal);
				img->data = dst + sizeof(*pal);
				img->borrowed = true;
				return uncmp_len;
			}
			free(dst);
		}
	}
	return 0;
}

enum wu_error xyz_parse(struct mp_parser *mp, struct wuimg *img) {
	const uint8_t *header = mp_next_slice(mp, 4);
	if (header) {
		img->w = buf_endian16(header, little_endian);
		img->h = buf_endian16(header + 2, little_endian);
		img->channels = 1;
		img->bitdepth = 8;
		img->layout = pix_rgba;
		return wuimg_verify(img);
	}
	return wu_unexpected_eof;
}

enum wu_error xyz_open(struct mp_parser *mp) {
	const unsigned char magic[] = {'X', 'Y', 'Z', '1'};
	return fmt_sigcmp_mem(magic, sizeof(magic), mp);
}

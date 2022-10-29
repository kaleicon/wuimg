#include <stdlib.h>

#include <zlib.h>

#include "misc/common.h"
#include "raster/fmt.h"
#include "xyz.h"

void xyz_free(struct xyz_desc *desc, struct wuimg *img) {
	img->data = NULL;
	img->u.palette = NULL;
	free(desc->data);
}

bool xyz_decode(struct xyz_desc *desc, struct wuimg *img) {
	struct wuptr src = mp_next_remaining(&desc->mp, SIZE_MAX);
	if (src.len) {
		const size_t dst_len = sizeof(*img->u.palette) + wuimg_size(img);
		uint8_t *dst = malloc(dst_len);
		if (dst) {
			const size_t pal_items = ARRAY_LEN(img->u.palette->color);
			uint8_t *uncmp = dst + pal_items;
			uLong uncmp_len = (uLong)(dst_len - pal_items);
			uncompress(uncmp, &uncmp_len, src.ptr, (uLong)src.len);
			if (uncmp_len > pal_items*3) {
				wuimg_palette_set(img, (struct raster_pal *)dst);
				img->data = dst + sizeof(*img->u.palette);
				img->borrowed = true;
				raster_pal_from_rgb8(img->u.palette, uncmp,
					pal_items);
				desc->data = dst;
				return true;
			}
			free(dst);
		}
	}
	return false;
}

enum wu_error xyz_parse(struct xyz_desc *desc, struct wuimg *img) {
	const uint8_t *header = mp_next_slice(&desc->mp, 4);
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

enum wu_error xyz_open(struct xyz_desc *desc, const struct mp_parser mp) {
	desc->mp = mp;
	const unsigned char magic[] = {'X', 'Y', 'Z', '1'};
	return fmt_sigcmp_mem(magic, sizeof(magic), &desc->mp);
}

// SPDX-License-Identifier: 0BSD
#include <stdlib.h>
#include <string.h>

#include "tlg.h"
#include "misc/endian.h"
#include "misc/math.h"
#include "misc/mem.h"
#include "raster/strip.h"

struct dict {
	size_t pos;
	uint8_t data[0x1000];
};

const char * tlg_version_str(const enum tlg_version ver) {
	switch (ver) {
	case tlg_v5: return "TLG5.0";
	case tlg_v6: return "TLG6.0";
	}
	return "???";
}

static size_t correlate4(uint32_t *dst, const size_t w, size_t y,
const size_t y_limit) {
	while (y < y_limit) {
		uint32_t row_acc = 0;
		for (size_t x = 0; x < w; ++x) {
			const size_t pos = w*y + x;
			uint32_t pix = dst[pos];
			uint32_t green;
			if (which_end() == little_endian) {
				green = ((pix >> 8) & 0xff) * 0x10001;
			} else {
				green = ((pix >> 16) & 0xff) * 0x1000100;
			}
			row_acc = uadd8_32(row_acc, uadd8_32(pix, green));
			pix = uadd8_32(row_acc, y ? dst[pos - w] : 0);
			dst[pos] = pix;
		}
		++y;
	}
	return y;
}

static size_t correlate3(uint8_t *dst, const size_t w, size_t y,
const size_t y_limit) {
	const uint8_t ch = 3;
	const size_t stride = w * ch;
	while (y < y_limit) {
		uint32_t row_acc = 0;
		for (size_t x = 0; x < w; ++x) {
			const size_t d = stride*y + x*ch;
			uint32_t pix = dst[d] << 16 | dst[d+1] << 8 | dst[d+2];
			const uint32_t green = dst[d+1] * 0x10001;
			row_acc = uadd8_32(row_acc, uadd8_32(pix, green));
			const size_t dw = d - w;
			pix = uadd8_32(row_acc, y
				? dst[dw] << 16 | dst[dw+1] << 8 | dst[dw+2]
				: 0);
			dst[d] = (uint8_t)(pix >> 16);
			dst[d+1] = (uint8_t)(pix >> 8);
			dst[d+2] = (uint8_t)pix;
		}
		++y;
	}
	return y;
}

static size_t pixel_correlate(uint8_t *dst, const size_t w, size_t y,
const size_t y_limit, const uint8_t ch) {
	return (ch == 4)
		? correlate4((uint32_t *)dst, w, y, y_limit)
		: correlate3(dst, w, y, y_limit);
}

#define MAX_LZSS_READ (3*8 + 1)
static void lzss_decomp_spread(uint8_t *restrict dst, const size_t dst_len,
const uint8_t *restrict src, size_t src_len, struct dict *dict,
const uint8_t ch) {
	const uint16_t dict_mask = 0xfff;
	size_t d = 0;
	size_t s = 0;
	uint8_t end[MAX_LZSS_READ * 2];
	while (d < dst_len) {
		if (s + MAX_LZSS_READ > src_len) {
			if (src == end) {
				break;
			}
			src = mem_bufswitch(src, &s, &src_len, end,
				sizeof(end));
		}
		uint8_t flags = src[s];
		++s;
		for (int i = 0; i < 8; ++i, flags >>= 1) {
			if (flags & 1) {
				const uint16_t off_len = buf_endian16(src + s, little_endian);
				s += 2;

				uint16_t count = (off_len >> 12) + 3;
				if (count == 0x12) {
					count += src[s];
					++s;
				}
				if (d + count >= dst_len) {
					return;
				}
				for (uint16_t j = 0; j < count; ++j) {
					const uint8_t byte = dict->data[(off_len + j) & dict_mask];
					dict->data[dict->pos] = byte;
					dst[d*ch] = byte;

					dict->pos = (dict->pos + 1) & dict_mask;
					++d;
				}
			} else {
				if (d >= dst_len) {
					return;
				}
				dst[d*ch] = src[s];
				dict->data[dict->pos] = src[s];

				dict->pos = (dict->pos + 1) & dict_mask;
				++s;
				++d;
			}
		}
	}
}

static size_t decode_blocks(const struct tlg_desc *desc, struct wuimg *img,
struct dict *dict, struct mparser *mp) {
	/* TLG v5 data stream:
		Offset  Type    Name
		0       struct  Blocks[BlockCount][Channels]

	 * Block format:
		Offset  Type    Name
		0       bool    IsUncompressed
		1       u32     BlockSize
		5       u8      Data[BlockSize]
	*/

	size_t y = 0;
	do {
		uint8_t *strip = img->data + y * img->w * img->channels;
		const size_t strip_height = zumin(img->h - y, desc->block_height);
		const size_t strip_pixs = strip_height * img->w;
		for (uint8_t z = 0; z < img->channels; ++z) {
			const uint8_t *header = mp_next_slice(mp, 5);
			if (!header) {
				return y;
			}

			const bool uncompressed = header[0];
			struct wuptr block = mp_next_remaining(mp,
				buf_endian32(header + 1, little_endian));

			if (uncompressed) {
				if (block.len > strip_pixs) {
					block.len = strip_pixs;
				}
				strip_spread(strip + z, block.ptr, block.len,
					img->channels);
			} else {
				lzss_decomp_spread(strip + z, strip_pixs,
					block.ptr, block.len, dict, img->channels);
			}
		}
		y = pixel_correlate(img->data, img->w, y, y + strip_height,
			img->channels);
	} while (y < img->h);
	return img->h;
}

static size_t decode_v5(const struct tlg_desc *desc, struct wuimg *img,
struct mparser *mp) {
	size_t w = 0;
	if (wuimg_alloc_noverify(img)) {
		struct dict *dict = calloc(1, sizeof(*dict));
		if (dict) {
			w = decode_blocks(desc, img, dict, mp);
			free(dict);
		}
	}
	return w;
}

size_t tlg_decode(const struct tlg_desc *desc, struct wuimg *img) {
	struct mparser mp = desc->mp;
	switch (desc->version) {
	case tlg_v5: return decode_v5(desc, img, &mp);
	case tlg_v6: break;
	}
	return 0;
}

static enum wu_error read_v5_header(struct tlg_desc *desc, struct wuimg *img) {
	/* TLG v5 header (after common header):
		Offset  Type    Name
		0       u32     BlockHeight
		4       u32     BlockSizes[ceildiv(Height, BlockHeight)]
	*/

	const uint8_t *header = mp_next_slice(&desc->mp, 4);
	if (!header) {
		return wu_unexpected_eof;
	}

	desc->block_height = buf_endian32(header, little_endian);
	if (!desc->block_height) {
		return wu_invalid_header;
	}

	/* BlockSizes are repeated in the data stream, so they can safely be
	 * skipped. */
	const size_t blocks = zuceildiv(img->h, desc->block_height);
	return mp_next_slice(&desc->mp, blocks * 4)
		? wu_ok : wu_unexpected_eof;
}

static enum wu_error validate_dims(struct tlg_desc *desc, struct wuimg *img,
const uint8_t ch, const uint32_t width, const uint32_t height) {
	switch (ch) {
	case 1:
		if (desc->version != tlg_v6) {
			return wu_invalid_header;
		}
		break;
	case 3: case 4:
		break;
	default:
		return wu_invalid_header;
	}

	if (width < 1 || height < 1) {
		return wu_invalid_header;
	}

	img->w = width;
	img->h = height;
	img->channels = ch;
	img->bitdepth = 8;
	img->layout = pix_bgra;
	return wuimg_verify(img);
}

enum wu_error tlg_read_header(struct tlg_desc *desc, struct wuimg *img) {
	/* Common TLG header, after tagged data:
		Offset  Type    Name
		0       u8      ColorChannels
		1       u32     Width
		5       u32     Height
		9
	*/

	const uint8_t *header = mp_next_slice(&desc->mp, 9);
	if (!header) {
		return wu_unexpected_eof;
	}

	enum wu_error st = validate_dims(desc, img, header[0],
		buf_endian32(header + 1, little_endian),
		buf_endian32(header + 5, little_endian));
	if (st != wu_ok) {
		return st;
	}

	switch (desc->version) {
	case tlg_v5: return read_v5_header(desc, img);
	case tlg_v6: break;
	}
	return wu_unsupported_feature;
}

enum wu_error tlg_open_mem(struct tlg_desc *desc, const struct map_info *map) {
	const unsigned char tlg[] = {'T', 'L', 'G'};
	const unsigned char sds[] = {'.', '0', 0, 's', 'd', 's', 0x1a};
	const unsigned char raw[] = {'.', '0', 0, 'r', 'a', 'w', 0x1a};

	desc->mp = mp_map(*map);
	const size_t siglen = sizeof(tlg) + sizeof(sds) + 1;
	const uint8_t *magic = mp_next_slice(&desc->mp, siglen);
	if (magic) {
		if (!memcmp(magic, tlg, sizeof(tlg))) {
			switch (magic[3]) {
			case '0':
				if (!memcmp(magic + 4, sds, sizeof(sds))) {
					return wu_unsupported_feature;
				}
				break;
			case tlg_v5: case tlg_v6:
				if (!memcmp(magic + 4, raw, sizeof(raw))) {
					desc->version = magic[3];
					return wu_ok;
				}
				break;
			}
		}
		return wu_invalid_signature;
	}
	return wu_unexpected_eof;
}

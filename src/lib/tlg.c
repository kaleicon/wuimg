#include <stdlib.h>
#include <string.h>

#include "tlg.h"
#include "common/endian.h"
#include "common/math.h"
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

static size_t pixel_correlate(uint8_t *dst, const size_t w, const uint8_t ch,
size_t y, const size_t y_limit) {
	const size_t stride = w * ch;
	while (y < y_limit) {
		uint8_t row_acc[4] = {0};
		for (size_t x = 0; x < w; ++x) {
			uint8_t *pix = dst + stride*y + x*ch;
			pix[0] += pix[1];
			pix[2] += pix[1];
			for (uint8_t z = 0; z < ch; ++z) {
				row_acc[z] += pix[z];
				pix[z] = row_acc[z];
				if (y) {
					pix[z] += pix[z - (long)stride];
				}
			}
		}
		++y;
	}
	return y;
}

static void lzss_decomp_spread(uint8_t *restrict dst, const size_t dst_len,
const uint8_t *restrict src, const size_t src_len, struct dict *dict,
const uint8_t ch) {
	const uint16_t dict_mask = 0xfff;
	size_t d = 0;
	size_t s = 0;
	while (d < dst_len && s < src_len) {
		uint8_t flags = src[s];
		++s;
		for (int i = 0; i < 8; ++i, flags >>= 1) {
			if (flags & 1) {
				if (s + 2 > src_len) {
					return;
				}
				const uint16_t off_len = buf_endian16(src + s, little_endian);
				s += 2;

				uint16_t count = (off_len >> 12) + 3;
				if (count == 0x12) {
					if (s >= src_len) {
						return;
					}
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
				if (d >= dst_len || s >= src_len) {
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

static size_t decode_blocks(const struct tlg_desc *desc, struct raw_img *img,
struct dict *dict, struct mp_parser *mp) {
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
		y = pixel_correlate(img->data, img->w, img->channels, y,
			y + strip_height);
	} while (y < img->h);
	return img->h;
}

static size_t decode_v5(const struct tlg_desc *desc, struct raw_img *img,
struct mp_parser *mp) {
	size_t w = 0;
	if (raw_img_alloc_noverify(img)) {
		struct dict *dict = calloc(1, sizeof(*dict));
		if (dict) {
			w = decode_blocks(desc, img, dict, mp);
			free(dict);
		}
	}
	return w;
}

size_t tlg_decode(const struct tlg_desc *desc, struct raw_img *img) {
	struct mp_parser mp = desc->mp;
	switch (desc->version) {
	case tlg_v5: return decode_v5(desc, img, &mp);
	case tlg_v6: break;
	}
	return 0;
}

static enum wu_error read_v5_header(struct tlg_desc *desc, struct raw_img *img) {
	/* TLG v5 header (after common header):
		Offset  Type    Name
		0       u32     BlockHeight
		4       u32     BlockSizes[(Height - 1) / BlockHeight + 1]
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
	const size_t blocks = (img->h - 1) / desc->block_height + 1;
	return mp_next_slice(&desc->mp, blocks * 4)
		? wu_ok : wu_unexpected_eof;
}

static enum wu_error validate_dims(struct tlg_desc *desc, struct raw_img *img,
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
	return raw_img_verify(img);
}

enum wu_error tlg_read_header(struct tlg_desc *desc, struct raw_img *img) {
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

enum wu_error tlg_open_mem(struct tlg_desc *desc, const struct mp_parser mp) {
	const unsigned char tlg[] = {'T', 'L', 'G'};
	const unsigned char sds[] = {'.', '0', 0, 's', 'd', 's', 0x1a};
	const unsigned char raw[] = {'.', '0', 0, 'r', 'a', 'w', 0x1a};

	desc->mp = mp;
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

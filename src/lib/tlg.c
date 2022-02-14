#include <stdlib.h>
#include <string.h>

#include "tlg.h"
#include "../raster/unpack.h"

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

static size_t decode_blocks(const struct tlg_desc *desc, uint8_t *restrict dst,
struct dict *dict, struct mem_parser *mp) {
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
		uint8_t *strip = dst + y * desc->r.w * desc->r.ch;
		const size_t strip_height = zumin(desc->r.h - y, desc->block_height);
		const size_t strip_pixs = strip_height * desc->r.w;
		for (uint8_t z = 0; z < desc->r.ch; ++z) {
			const uint8_t *header = mem_next_slice(mp, 5);
			if (!header) {
				return y;
			}

			const bool uncompressed = header[0];
			struct wuptr block = mem_next_remaining(mp,
				buf_endian32(header + 1, little_endian));

			if (uncompressed) {
				if (block.len > strip_pixs) {
					block.len = strip_pixs;
				}
				strip_spread(strip + z, block.ptr, block.len, desc->r.ch);
			} else {
				lzss_decomp_spread(strip + z, strip_pixs,
					block.ptr, block.len, dict, desc->r.ch);
			}
		}
		y = pixel_correlate(dst, desc->r.w, desc->r.ch, y, y + strip_height);
	} while (y < desc->r.h);
	return desc->r.h;
}

static size_t decode_v5(const struct tlg_desc *desc, uint8_t *restrict dst,
struct mem_parser *mp) {
	size_t w = 0;
	struct dict *dict = calloc(1, sizeof(*dict));
	if (dict) {
		w = decode_blocks(desc, dst, dict, mp);
		free(dict);
	}
	return w;
}

size_t tlg_decode(struct tlg_desc *desc, void *restrict dst) {
	switch (desc->version) {
	case tlg_v5: return decode_v5(desc, dst, &desc->mp);
	case tlg_v6: break;
	}
	return 0;
}

static enum lib_fail read_v5_header(struct tlg_desc *desc) {
	/* TLG v5 header (after common header):
		Offset  Type    Name
		0       u32     BlockHeight
		4       u32     BlockSizes[(Height - 1) / BlockHeight + 1]
	*/

	const uint8_t *header = mem_next_slice(&desc->mp, 4);
	if (!header) {
		return lib_unexpected_eof;
	}

	desc->block_height = buf_endian32(header, little_endian);
	if (!desc->block_height) {
		return lib_invalid_header;
	}

	/* BlockSizes are included in the data stream, so they can safely be
	 * skipped. */
	const size_t blocks = (desc->r.h - 1) / desc->block_height + 1;
	return mem_next_slice(&desc->mp, blocks * 4)
		? lib_ok : lib_unexpected_eof;
}

static enum lib_fail validate_dims(struct tlg_desc *desc, const uint8_t ch,
const uint32_t width, const uint32_t height) {
	switch (ch) {
	case 1:
		if (desc->version != tlg_v6) {
			return lib_invalid_header;
		}
		break;
	case 3: case 4:
		break;
	default:
		return lib_invalid_header;
	}

	if (width < 1 || height < 1) {
		return lib_invalid_header;
	}

	desc->r = (struct raster_desc) {
		.w = width,
		.h = height,
		.ch = ch,
		.bitdepth = 8,
		.layout = pix_bgra,
	};
	raster_normalize(&desc->r);
	return lib_ok;
}

enum lib_fail tlg_read_header(struct tlg_desc *desc) {
	if (desc->tagged_data) {
		return lib_unsupported_feature;
	}

	/* Common TLG header, after tagged data:
		Offset  Type    Name
		0       u8      ColorChannels
		1       u32     Width
		5       u32     Height
		9
	*/

	const uint8_t *header = mem_next_slice(&desc->mp, 9);
	if (!header) {
		return lib_unexpected_eof;
	}

	enum lib_fail st = validate_dims(desc, header[0],
		buf_endian32(header + 1, little_endian),
		buf_endian32(header + 5, little_endian));
	if (st != lib_ok) {
		return st;
	}

	switch (desc->version) {
	case tlg_v5: return read_v5_header(desc);
	case tlg_v6: return lib_unsupported_feature;
	}
	return lib_ok;
}

enum lib_fail tlg_open_mem(struct tlg_desc *desc, const struct map_info *map) {
	const unsigned char tlg[] = {'T', 'L', 'G'};
	const unsigned char sds[] = {'.', '0', 0, 's', 'd', 's', 0x1a};
	const unsigned char raw[] = {'.', '0', 0, 'r', 'a', 'w', 0x1a};

	desc->mp = mem_parser_mem(map->len, map->data);
	desc->tagged_data = false;

	const size_t siglen = sizeof(tlg) + sizeof(sds) + 1;
	const uint8_t *magic = mem_next_slice(&desc->mp, siglen);
	if (magic) {
		if (!memcmp(magic, tlg, sizeof(tlg))) {
			switch (magic[3]) {
			case '0':
				if (!memcmp(magic + 4, sds, sizeof(sds))) {
					desc->tagged_data = true;
					return lib_ok;
				}
				break;
			case tlg_v5: case tlg_v6:
				if (!memcmp(magic + 4, raw, sizeof(raw))) {
					desc->version = magic[3];
					return lib_ok;
				}
				break;
			}
		}
		return lib_invalid_signature;
	}
	return lib_unexpected_eof;
}

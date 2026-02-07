// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2026 kaleido
#include <stdlib.h>
#include <string.h>

#include "lib/eri.h"
#include "misc/bit.h"
#include "misc/endian.h"
#include "misc/math.h"
#include "misc/mem.h"

/* Entis Rasterized Image.
 * Despite the enums below, this decoder only does Gamma encoded lossless
 * images.

 * Referenced from erina_rtf, erisalib 1.07, and gimp-eri
https://www.entis.jp/eridev/download/index.html
https://github.com/Barracuda72/gimp-eri
*/

// Use a much faster gamma decoder that's harder to understand
static const bool FAST_GAMMA_DECODE = false;

/* Types from xerisa.h */
enum eri_version {
	eri_standard_version = 0x020100,
	eri_enhanced_version = 0x020200,
};

enum eri_cvtype {
	eri_cvtype_lossless_eri = 0x03020000,
	eri_cvtype_dct_eri = 1,
	eri_cvtype_lot_eri = 5,
	eri_cvtype_lot_eri_mss = 0x0105,
};

enum eri_architecture {
	eri_arithmetic_code   = 32,
	eri_runlength_gamma   = (int32_t)0xffffffff,
	eri_runlength_huffman = (int32_t)0xfffffffc,
	erisa_nemesis_code    = (int32_t)0xfffffff0,
};

enum eri_type_with {
	eri_with_palette = 0x01 << 24,
	eri_use_clipping = 0x02 << 24,
	eri_with_alpha = 0x04 << 24,
};

enum eri_type {
	eri_rgb_image = 1,
	eri_gray_image = 2,
};

struct eri_gamma_state {
	unsigned unpack;
	uint32_t rem;
};

/* Max seen is 5, and it's unlikely it goes higher than that due to the buffer
 * sizes involved. */
static const uint32_t MAX_BLOCKING_DEGREE = 6;

/* ERI chunks like a 64-bit version of IFF, nesting included.
 * Don't known if there are padding rules, as all chunks other than the
 * compressed stream at the end have sizes multiple of four. */
struct eri_chunk {
	uint8_t name[8];
	uint64_t len;
};

struct eri_parser;

typedef struct wu_st (*eri_parser_fn_t)(struct eri_parser *state, void *ptr,
	struct eri_chunk);

struct eri_parser_table {
	uint8_t name[8];
	eri_parser_fn_t fn;
};

struct eri_parser {
	const struct eri_parser_table *table;
	size_t table_len;
	void *user;
};

static struct wu_st eri_parser_next(struct eri_parser *state,
struct mparser *mp, struct eri_chunk chunk) {
	const uint8_t *hdr = mp_slice(mp, sizeof(chunk));
	if (!hdr) {
		return wuerr(wu_unexpected_eof, "EOF while reading chunk header");
	}
	memcpy(chunk.name, hdr, sizeof(chunk.name));
	chunk.len = buf_endian64(hdr + sizeof(chunk.name),
		little_endian);

	for (size_t i = 0; i < state->table_len; ++i) {
		const struct eri_parser_table *t = state->table + i;
		if (!memcmp(chunk.name, t->name, sizeof(t->name))) {
			return t->fn(state, state->user, chunk);
		}
	}
	return wuerr(wu_uncertain_validity, "unexpected or unknown ERI chunk");
}


static void tile_cpy(uint8_t *restrict dst, const uint8_t *restrict dec_plane,
const size_t dst_stride, const unsigned block_side, const size_t tw,
const size_t th) {
	for (size_t y = 0; y < th; ++y) {
		memcpy(dst + y*dst_stride, dec_plane + y*block_side, tw);
	}
}

static void array_add1(uint8_t *restrict dst,
const uint8_t *restrict src, const unsigned len) {
	for (unsigned i = 0; i < len; ++i) {
		dst[i] += src[i];
	}
}

static void array_add2(uint8_t *restrict dst1, uint8_t *restrict dst2,
const uint8_t *restrict src, const unsigned len) {
	for (unsigned i = 0; i < len; ++i) {
		dst1[i] += src[i];
		dst2[i] += src[i];
	}
}

static void vert_decorrelate(uint8_t *restrict dec_plane,
uint8_t *restrict prev_row, const unsigned block_side) {
	const uint8_t *prev_ptr = prev_row;
	for (unsigned y = 0; y < block_side; ++y) {
		uint8_t *dst = dec_plane + y*block_side;
		array_add1(dst, prev_ptr, block_side);
		prev_ptr = dst;
	}
	memcpy(prev_row, prev_ptr, block_side);
}

static void horz_decorrelate(uint8_t *restrict dec_plane,
uint8_t *restrict prev_column, const unsigned block_side) {
	for (unsigned y = 0; y < block_side; ++y) {
		uint8_t last = prev_column[y];
		uint8_t *dst = dec_plane + y*block_side;
		for (unsigned x = 0; x < block_side; ++x) {
			last += dst[x];
			dst[x] = last;
		}
		prev_column[y] = last;
	}
}

static void color_decorrelate2(uint8_t *base, const unsigned len,
const unsigned dst1_z, const unsigned dst2_z, const unsigned src_z) {
	array_add2(base + dst1_z*len, base + dst2_z*len,
		base + src_z*len, len);
}

static void color_decorrelate1(uint8_t *base, const unsigned len,
const unsigned dst_z, const unsigned src_z) {
	array_add1(base + dst_z*len, base + src_z*len, len);
}

static void color_decorrelate(uint8_t *restrict dec_buf, const unsigned area,
const unsigned op) {
	switch (op) {
	case 15: // RG += B
		color_decorrelate2(dec_buf, area, 0, 1, 2);
		return;
	case 14: // G += B
		color_decorrelate1(dec_buf, area, 1, 2);
		return;
	case 13: // R += B
		color_decorrelate1(dec_buf, area, 0, 2);
		return;
	case 11: // RB += G
		color_decorrelate2(dec_buf, area, 0, 2, 1);
		return;
	case 10: // B += G
		color_decorrelate1(dec_buf, area, 2, 1);
		return;
	case 9: // R += G
		color_decorrelate1(dec_buf, area, 0, 1);
		return;
	case 7: // GB += R
		color_decorrelate2(dec_buf, area, 1, 2, 0);
		return;
	case 6: // B += R
		color_decorrelate1(dec_buf, area, 2, 0);
		return;
	case 5: // G += R
		color_decorrelate1(dec_buf, area, 1, 0);
		return;
	}
}

static uint32_t read_gamma_interleaved(struct bitstrm *bs) {
	/* Interleaved variant of Gamma encoding.
	 * Start value is 1. If next bit is 1, shift the next bit into the
	 * value, otherwise stop.
	 * Why interleave bits though? */
	uint32_t val = 1;
	if (FAST_GAMMA_DECODE) {
		int peeks = 0;
		uint32_t i;
		do {
			uint32_t bits = bitstrm_msb_peek_32(bs);
			i = 1;
			while ((bits >> 30) & 2) {
				val = (val << 1) | ((bits >> 30) & 1);
				bits <<= 2;
				i += 2;
			}
			bitstrm_seek(bs, i);
			++peeks;
		} while (i == 31 && peeks < 2);
	} else {
		while (bitstrm_msb_next(bs)) {
			val = (val << 1) | bitstrm_msb_next(bs);
		}
	}
	return val;
}

static void decode_gamma_bytes(uint8_t *restrict dec_buf, uint32_t dec_len,
struct bitstrm *bs, struct eri_gamma_state *gamma) {
	uint32_t d = 0;
	do {
		if (!gamma->rem) {
			gamma->unpack ^= 1;
			gamma->rem = read_gamma_interleaved(bs);
		}
		uint32_t cnt = u32min(dec_len - d, gamma->rem);
		gamma->rem -= cnt;
		if (gamma->unpack) {
			for (uint32_t i = 0; i < cnt; ++i) {
				uint32_t sign = -(uint32_t)bitstrm_msb_next(bs);
				uint32_t byte = read_gamma_interleaved(bs);
				dec_buf[d+i] = (uint8_t)((byte ^ sign) - sign);
			}
		} else {
			memset(dec_buf + d, 0, cnt);
		}
		d += cnt;
	} while (d < dec_len);
}

struct eri_mtf {
	uint8_t v[0x100];
};

static struct wu_st imagefrm_v2(struct eri_desc *desc, struct bitstrm *bs) {
	const size_t tables = 0x101;
	struct eri_mtf *mtf = malloc(sizeof(*mtf) * tables);
	if (!mtf) {
		return WUERR_HERE(wu_alloc_error);
	}
	for (size_t i = 0; i < tables; ++i) {
		for (size_t k = 0; k < sizeof(mtf[i].v); ++k) {
			mtf[i].v[k] = (uint8_t)k;
		}
	}
	struct wuimg *img = desc->img;
	const size_t stride = wuimg_stride(img);
	size_t y = 0;
	while (y < img->h) {
		unsigned table = 0x100;
		uint8_t *dst = img->data + y*stride;
		size_t x = 0;
		while (x < img->w) {
			const uint8_t idx = (uint8_t)(
				read_gamma_interleaved(bs) - 1
			);
			const uint8_t val = memcycle(mtf[table].v, idx);
			dst[x] = val;
			++x;
			if (val == table) {
				const size_t cnt = zumin(
					read_gamma_interleaved(bs) - 1,
					img->w - x
				);
				memset(dst + x, val, cnt);
				x += cnt;
			} else {
				table = val;
			}
		}
		++y;
	}
	free(mtf);
	return WU_OK;
}

static struct wu_st imagefrm_v1(struct eri_desc *desc, struct bitstrm *bs) {
	struct wuimg *img = desc->img;
	unsigned b_degree = desc->blocking_degree;
	unsigned block_side = 1u << b_degree;
	size_t x_blocks = (img->w + block_side - 1) >> b_degree;
	size_t y_blocks = (img->h + block_side - 1) >> b_degree;

	const size_t blocks = x_blocks * y_blocks;

	const uint8_t *ops = NULL;
	if (img->channels >= 3) {
		size_t op_table_bitlen = blocks*4;
		if (bs->len <= op_table_bitlen) {
			return wuerr(wu_unexpected_eof,
				"EOF while reading gamma op codes");
		}
		ops = bs->buf;
		bitstrm_seek(bs, op_table_bitlen);
	}

	uint32_t pair = bitstrm_msb_adv(bs, 2);
	if (pair & 0x2) {
		return wuerr(wu_uncertain_validity,
			"first stream bit == 1");
	}
	struct eri_gamma_state gamma = {
		.unpack = (pair & 0x1) ^ 0x1,
	};

	unsigned block_area = block_side << b_degree;
	unsigned block_samples = block_area * img->channels;
	size_t prev_row_size = x_blocks * block_side * img->channels;
	size_t prev_column_size = block_side * img->channels;
	uint8_t *dec_buf = malloc(block_samples
		+ prev_row_size
		+ prev_column_size
	);
	if (!dec_buf) {
		return wuerr(wu_alloc_error, "failed to allocate dec buffers");
	}
	uint8_t *prev_row = dec_buf + block_samples;
	uint8_t *prev_column = prev_row + prev_row_size;

	const size_t stride = img->u.planes->p[0].stride;
	memset(prev_row, 0, prev_row_size);
	for (size_t ty = 0; ty < y_blocks; ++ty) {
		memset(prev_column, 0, prev_column_size);
		const size_t y = ty*block_side;
		const size_t th = (y_blocks - 1 == ty)
			? img->h - y
			: block_side;
		for (size_t tx = 0; tx < x_blocks; ++tx) {
			const size_t tw = (x_blocks - 1 == tx)
				? img->w - tx*block_side
				: block_side;

			decode_gamma_bytes(dec_buf, block_samples, bs, &gamma);

			if (img->channels >= 3) {
				size_t op_idx = ty*x_blocks + tx;
				unsigned op = 0;
				op = ops[op_idx/2] >> ((op_idx & 1) ? 0 : 4);
				op = (op & 0xf);
				color_decorrelate(dec_buf, block_area, op);
			}

			for (uint8_t z = 0; z < img->channels; ++z) {
				uint8_t *dec_plane = dec_buf + z*block_area;
				horz_decorrelate(dec_plane,
					prev_column + z*block_side, block_side);
				vert_decorrelate(dec_plane,
					prev_row + tx*block_side*img->channels
						+ z*block_side,
					block_side);

				uint8_t *dst = img->data + z*stride*img->h
					+ y*stride
					+ tx*block_side;
				tile_cpy(dst, dec_plane, stride, block_side,
					tw, th);
			}
		}
	}
	free(dec_buf);
	return WU_OK;
}

static struct wu_st parse_imagefrm(struct eri_parser *state, void *user,
const struct eri_chunk chunk) {
	/* "ImageFrm" struct:
		Offset  Type    Name
		0       u8      Version
		1       u8      OpTableCompression
		2       u8      EncodeType
		3       u8      BitCount
		4       u8      Stream[]
	*/
	(void)state;
	struct eri_desc *desc = user;
	const uint8_t *buf = mp_slice(&desc->mp, 4);
	if (!buf) {
		return WUERR_HERE(wu_unexpected_eof);
	}

	struct bitstrm bs;
	bitstrm_from_wuptr(&bs, mp_avail(&desc->mp, chunk.len));

	switch (buf[0]) {
	case 1:
		;const uint8_t encode_type = buf[2];
		if (encode_type != 1) {
			return wuerr(wu_unsupported_feature,
				"ImageFrm v1: encode_type != 1");
		} else if (buf[1] != 0) {
			return wuerr(wu_invalid_header,
				"ImageFrm v1: op_table_compression != 0");
		} else if (buf[3] != 0) {
			return wuerr(wu_invalid_header,
				"ImageFrm v1: bit_count != 0");
		} else if (!desc->blocking_degree) {
			return wuerr(wu_invalid_header,
				"ImageFrm v1: blocking degree = 0");
		}
		return imagefrm_v1(desc, &bs);
	case 2:
		if (buf[1] | buf[2] | buf[3]) {
			return wuerr(wu_invalid_header,
				"ImageFrm v2: non-zero reserved fields");
		}
		return imagefrm_v2(desc, &bs);
	}
	return wuerr(wu_unsupported_feature, "unsupported ImageFrm version");
}

static const struct eri_parser_table ERI_IMAGEFRM = {
	{'I', 'm', 'a', 'g', 'e', 'F', 'r', 'm'}, parse_imagefrm,
};

struct wu_st eri_decode(struct eri_desc *desc) {
	struct eri_parser state = {
		.table = &ERI_IMAGEFRM,
		.table_len = 1,
		.user = desc,
	};
	struct eri_chunk dummy = {0};
	return eri_parser_next(&state, &desc->mp, dummy);
}

static struct wu_st parse_palette(struct eri_parser *state, void *user,
const struct eri_chunk chunk) {
	(void)state;
	if (chunk.len % 4) {
		return wuerr(wu_invalid_header, "odd palette size");
	}
	struct eri_desc *desc = user;
	const uint8_t *hdr = mp_slice(&desc->mp, chunk.len);
	if (!hdr) {
		return WUERR_HERE(wu_unexpected_eof);
	}
	struct palette *pal = desc->img->u.palette;
	memcpy(pal->color, hdr, zumin(chunk.len, sizeof(pal->color)));
	return WU_OK;
}

static struct wu_st parse_stream(struct eri_parser *state, void *user,
const struct eri_chunk chunk) {
	/* "Stream" chunks:
		"Palette "
		"ImageFrm"
	*/
	struct eri_desc *desc = user;
	desc->mp = mp_wuptr(mp_avail(&desc->mp, chunk.len));
	if (desc->img->mode == image_mode_palette) {
		++state->table;
		return eri_parser_next(state, &desc->mp, chunk);
	}
	return WU_OK;
}

static const struct eri_parser_table ERI_STREAM[] = {
	{{'S', 't', 'r', 'e', 'a', 'm', ' ', ' '}, parse_stream},
	{{'P', 'a', 'l', 'e', 't', 't', 'e', ' '}, parse_palette},
};

static struct wu_st parse_imageinf(struct eri_parser *state, void *user,
const struct eri_chunk chunk) {
	/* "ImageInf" struct:
		Offset  Type    Name
		0       u32     Version
		4       u32     Transformation
		8       u32     Architecture
		12      u32     ChannelCount
		16      u32     Width
		20      s32     Height           // image is bottom-up if < 0
		24      u32     Depth
		28      u32     ClippedPixel
		32      u32     SamplingFlags
		36      u32     QuantizedBits[2]
		44      u32     AllotedBits[2]
		52      u32     BlockingDegree
		56      u32     LappedTransform
		60      u32     FrameTransform
		64      u32     FrameDegree
		68
	*/
	if (chunk.len != 68) {
		return wuerr(wu_invalid_header, "'ImageInf' chunk length != 68");
	}
	struct eri_desc *desc = user;
	const uint8_t *buf = mp_slice(&desc->mp, chunk.len);
	if (!buf) {
		return WUERR_HERE(wu_unexpected_eof);
	}
	const uint32_t version = buf_endian32l(buf),
		transform = buf_endian32l(buf + 4),
		architecture = buf_endian32l(buf + 8),
		ch_count = buf_endian32l(buf + 12),
		depth = buf_endian32l(buf + 24),
		blocking_degree = buf_endian32l(buf + 52);
	const char *err = NULL;
	if (version != eri_standard_version) {
		err = "ImageInf: non-standard version";
	} else if (transform != eri_cvtype_lossless_eri) {
		err = "only Lossless-ERI supported";
	} else if ((enum eri_architecture)architecture != eri_runlength_gamma) {
		err = "only Run-length Gamma supported";
	} else if (blocking_degree > MAX_BLOCKING_DEGREE) {
		err = "blocking degree too high";
	}
	if (err) {
		return wuerr(wu_unsupported_feature, err);
	}

	desc->blocking_degree = (uint8_t)blocking_degree;
	struct wuimg *img = desc->img;
	img->w = buf_endian32l(buf + 16);
	int32_t h = (int32_t)buf_endian32l(buf + 20);
	img->h = (size_t)labs(h);
	img->mirror = h >= 0;
	img->bitdepth = 8;
	img->layout = pix_bgra;
	img->alpha = alpha_associated;
	switch (ch_count) {
	case eri_gray_image:
		if (depth != 8) {
			return wuerr(wu_invalid_header, "Gray depth != 8");
		}
		img->channels = 1;
		img->layout = pix_gray;
		break;
	case eri_rgb_image:
		if (depth != 24) {
			return wuerr(wu_invalid_header, "RGB depth != 24");
		}
		img->channels = 3;
		break;
	case eri_rgb_image | eri_with_alpha:
		if (depth != 32) {
			return wuerr(wu_invalid_header, "RGBA depth != 32");
		}
		img->channels = 4;
		break;
	case eri_rgb_image | eri_with_palette:
		if (depth != 8) {
			return wuerr(wu_invalid_header, "Palette depth != 8");
		}
		img->channels = 1;
		img->alpha = alpha_ignore;
		if (!wuimg_palette_init(img)) {
			return WUERR_HERE(wu_alloc_error);
		}
		break;
	default:
		return wuerr(wu_unsupported_feature,
			"unsupported color format");
	}
	if (!img->u.palette && !wuimg_plane_init(img)) {
		return WUERR_HERE(wu_alloc_error);
	}

	state->table = ERI_STREAM;
	state->table_len = 1;
	return eri_parser_next(state, &desc->mp, chunk);
}

static struct wu_st parse_filehdr(struct eri_parser *state, void *user,
const struct eri_chunk chunk) {
	/* "FileHdr" struct:
		Offset  Type    Name
		0       u32     Version
		4       u32     ContainedFlag
		8       u32     Keyframes
		12      u32     Frames
		16      u32     AllFrameTime
		20
	*/
	if (chunk.len != 20) {
		return wuerr(wu_invalid_header, "'FileHdr' chunk length != 20");
	}
	struct eri_desc *desc = user;
	const uint8_t *buf = mp_slice(&desc->mp, chunk.len);
	if (!buf) {
		return WUERR_HERE(wu_unexpected_eof);
	}
	const uint32_t version = buf_endian32l(buf),
		keyframes = buf_endian32l(buf + 8),
		frames = buf_endian32l(buf + 12);
	if (version != eri_standard_version) {
		return wuerr(wu_unsupported_feature, "FileHdr: non-standard version");
	} else if (keyframes != 1 || frames != 1) {
		return wuerr(wu_unsupported_feature, "multiple frames");
	}
	++state->table;
	return eri_parser_next(state, &desc->mp, chunk);
}

static const struct eri_parser_table ERI_HEADER_SECTIONS[] = {
	{{'F', 'i', 'l', 'e', 'H', 'd', 'r', ' '}, parse_filehdr},
	{{'I', 'm', 'a', 'g', 'e', 'I', 'n', 'f'}, parse_imageinf},
};

static struct wu_st parse_header(struct eri_parser *state, void *user,
const struct eri_chunk chunk) {
	/* "Header" struct:
		Offset  Type    Name
		0       chunk   FileHdr
		36      chunk   ImageInf
		120
	*/
	if (chunk.len != 120) {
		return wuerr(wu_invalid_header, "'Header' chunk length != 120");
	}
	struct eri_desc *desc = user;
	state->table = ERI_HEADER_SECTIONS;
	state->table_len = 1;
	return eri_parser_next(state, &desc->mp, chunk);
}

static const struct eri_parser_table ERI_HEADER = {
	{'H', 'e', 'a', 'd', 'e', 'r', ' ', ' '}, parse_header,
};

struct wu_st eri_init(struct eri_desc *desc, struct wuimg *img,
const struct wuptr map) {
	/* ERI header:
		Offset  Type    Name
		0       u8      Magic[8]
		8       u32     Version?
		12      u32     ???
		16      u8      MoreMagic[48]
		64
	*/
	desc->mp = mp_wuptr(map);
	desc->img = img;
	const uint8_t magic[] = {
		'E', 'n', 't', 'i', 's', 0x1a, 0, 0,
		0, 1, 0, 3, 0, 0, 0, 0,
	};
	const uint8_t more_magic[] = {
		'E', 'n', 't', 'i', 's', ' ',
		'R', 'a', 's', 't', 'e', 'r', 'i', 'z', 'e', 'd', ' ',
		'I', 'm', 'a', 'g', 'e',
	};
	const uint8_t *hdr = mp_slice(&desc->mp, 64);
	if (!hdr) {
		return WUERR_HERE(wu_unexpected_eof);
	} else if (memcmp(hdr, magic, sizeof(magic))) {
		return WUERR_HERE(wu_invalid_signature);
	} else if (memcmp(hdr + 16, more_magic, sizeof(more_magic))) {
		return wuerr(wu_unsupported_feature, "non-ERI image");
	}

	struct eri_parser state = {
		.table = &ERI_HEADER,
		.table_len = 1,
		.user = desc,
	};
	struct eri_chunk dummy = {0};
	return eri_parser_next(&state, &desc->mp, dummy);
}

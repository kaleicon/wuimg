// SPDX-License-Identifier: 0BSD
#include "misc/common.h"
#include "misc/decomp.h"
#include "misc/endian.h"
#include "misc/math.h"
#include "misc/mem.h"
#include "raster/graphics_adapters.h"

#include "lib/ilbm.h"

static const uint8_t HAM_CH = 3; // Output channels for HAM

void ilbm_cleanup(struct ilbm_desc *desc) {
	palette_unref(desc->pal);
}

void ilbm_palette_cycle(const struct ilbm_desc *desc, struct palette *pal,
const double time) {
	const struct palette *src_pal = desc->pal;
	for (size_t cycle = 0; cycle < desc->crng.len; ++cycle) {
		const struct ilbm_crng *crng = desc->crng.crng + cycle;
		if (!crng->active) {
			continue;
		}

		const size_t cnt = crng->cnt;
		size_t i = (size_t)(time/crng->secs) % cnt;
		if (crng->reverse) {
			i = cnt - i;
		}

		const uint8_t low = crng->low;
		palette_cyclecopy(pal, src_pal, low, i, cnt);
	}
}

static size_t interleave_bitplanes(const struct ilbm_desc *desc,
struct wuimg *img, uint8_t *restrict dst, const struct wuptr body,
const uint8_t planes) {
	const size_t outstride = img->w * (desc->ham ? 1 : img->channels);
	const size_t instride = strip_length(img->w, 1, 1) * desc->planes;
	const size_t rows = zumin(body.len / instride, img->h);
	for (size_t y = 0; y < rows; ++y) {
		bitplane_interleave_row(dst + y*outstride,
			body.ptr + y*instride, img->w, planes, 1);
	}
	return 1;
}

static void expand_ham(const struct ilbm_desc *desc, struct wuimg *img,
const size_t offset) {
	const uint8_t color_bits = (desc->planes > 6) ? 6 : 4;
	const uint8_t mask = (uint8_t)((1 << color_bits) - 1);
	const uint8_t antimask = (uint8_t)((1 << (8 - color_bits)) - 1);

	uint8_t *dst = img->data;
	const uint8_t *src = img->data + offset;
	const uint8_t *restrict pal = (uint8_t *)desc->pal;
	const size_t stride = img->w*HAM_CH;
	const uint8_t cpy_size = HAM_CH;
	for (size_t y = 0; y < img->h; ++y) {
		for (size_t x = 0; x < img->w; ++x) {
			const size_t pix = y*stride + x*HAM_CH;
			const uint8_t s = src[y*img->w + x];
			const uint8_t entry = s & mask;
			const uint8_t ham = s >> color_bits;

			if (ham) {
				if (x) {
					memcpy(dst + pix, dst + pix - HAM_CH,
						cpy_size);
				} else {
					memcpy(dst + pix, pal + 0, cpy_size);
				}
//				uint8_t ch = (ham ^ 2);
//				ch ^= ch >> 1;
				const uint8_t conv[4] = {0 /*unused*/, 2, 0, 1};
				const uint8_t ch = conv[ham];
				dst[pix + ch] = (uint8_t)(entry << (8 - color_bits))
					| (dst[pix + ch] & antimask);
			} else {
				memcpy(dst + pix, pal + entry*4, cpy_size);
			}
		}
	}
}

static size_t expand_body(const struct ilbm_desc *desc, struct wuimg *img,
const struct wuptr body) {
	size_t offset = 0;
	if (desc->ham) {
		offset = wuimg_size(img) - img->w*img->h;
	}
	interleave_bitplanes(desc, img, img->data + offset, body, desc->planes);
	if (desc->ham) {
		expand_ham(desc, img, offset);
	}
	return 1;
}

static size_t decompress_ilbm(const struct ilbm_desc *desc, struct wuimg *img,
const struct wuptr body) {
	const size_t upack_len = strip_length(img->w, 1, 1) * desc->planes * img->h;
	uint8_t *upack = malloc(upack_len);
	if (upack) {
		const size_t w = decomp_pack_bits(upack, upack_len,
			(const int8_t *)body.ptr, body.len);
		if (w) {
			expand_body(desc, img, wuptr_mem(upack, w));
		}
		free(upack);
		return w;
	}
	return 0;
}

static size_t ilbm_decode(const struct ilbm_desc *desc, struct wuimg *img,
const struct wuptr body) {
	if (desc->planes == 0) {
		return 1;
	}
	if (!wuimg_alloc_noverify(img)) {
		return 0;
	}
	switch (desc->format) {
	case ilbm_format_ilbm:
		switch (desc->compression) {
		case ilbm_compression_none:
			return expand_body(desc, img, body);
		case ilbm_compression_packbits:
			return decompress_ilbm(desc, img, body);
		}
		break;
	case ilbm_format_pbm:
		;const size_t size = wuimg_size(img);
		switch (desc->compression) {
		case ilbm_compression_none:
			memcpy(img->data, body.ptr, zumin(size, body.len));
			return 1;
		case ilbm_compression_packbits:
			return decomp_pack_bits(img->data, size,
				(const int8_t *)body.ptr, body.len);
		}
		break;
	}
	return 0;
}

size_t ilbm_decode_tiny(const struct ilbm_desc *desc, struct wuimg *main,
struct wuimg *tiny) {
	if (desc->tiny.present && wuimg_clone(tiny, main)) {
		tiny->w = desc->tiny.w;
		tiny->h = desc->tiny.h;
		if (wuimg_verify(tiny) == wu_ok) {
			return ilbm_decode(desc, tiny, desc->tiny.data);
		}
	}
	return 0;
}

size_t ilbm_decode_main(const struct ilbm_desc *desc, struct wuimg *img) {
	return ilbm_decode(desc, img, desc->body);
}

static enum wu_error tidy_up(struct ilbm_desc *desc, struct wuimg *img) {
	if (desc->planes == 0) {
		if (desc->colors == 0) {
			return wu_no_image_data;
		}
		img->data = (uint8_t *)desc->pal;
		memmove(img->data, desc->pal->color, sizeof(desc->pal->color));
		desc->pal = NULL;
		img->w = desc->colors;
		img->h = 1;
		img->channels = 4;
		img->bitdepth = 8;
	} else {
		if (desc->pal) {
			struct palette *pal = desc->pal;
			if (!desc->ham) {
				if (img->evolving) {
					wuimg_palette_set(img, palette_copy(pal));
				} else {
					wuimg_palette_set(img, pal);
					desc->pal = NULL;
				}

				if (desc->extra_half_brite) {
					for (size_t i = 0; i < 32; ++i) {
						pal->color[i+32] = (struct pix_rgba8) {
							.r = pal->color[i].r >> 1,
							.g = pal->color[i].g >> 1,
							.b = pal->color[i].b >> 1,
							.a = pal->color[i].a,
						};
					}
				}
				switch (desc->masking) {
				case ilbm_masking_bitplane:
					;const uint8_t items = (uint8_t)(1 << desc->planes);
					for (uint8_t i = 0; i < items; ++i) {
						memcpy(pal->color + items + i,
							pal->color + i,
							sizeof(*pal->color));
						pal->color[items + i].a = 0;
					}
					desc->planes += 1;
					break;
				case ilbm_masking_value:
					pal->color[desc->trans_value].a = 0;
					break;
				case ilbm_masking_none:
				case ilbm_masking_lasso:
					break;
				}
			}
		} else {
			if (desc->planes < 8) {
				img->used_bits = desc->planes;
			}
		}
	}
	return wuimg_verify(img);
}

static enum wu_error parse_tiny(struct ilbm_desc *desc, const uint32_t id,
const struct wuptr data) {
	/* TINY structure:
		Offset  Size    Name
		0       u16     Width
		2       u16     Height
		4       [Len-4] Data
	*/
	(void)id;
	if (data.len > 4) {
		desc->tiny = (struct ilbm_tiny) {
			.present = true,
			.w = buf_endian16(data.ptr, big_endian),
			.h = buf_endian16(data.ptr + 2, big_endian),
			.data = wuptr_mem(data.ptr + 4, data.len - 4),
		};
		return wu_ok;
	}
	return wu_invalid_header;
}

static enum wu_error parse_text(struct ilbm_desc *desc, const uint32_t id,
const struct wuptr data) {
	return (*desc->text_callback)(id, data, desc->usr_ptr);
}

static enum wu_error parse_crng(struct ilbm_desc *desc, struct wuimg *img,
const struct wuptr data) {
	/* CRNG structure:
		Offset  Size    Name
		0       u16     _pad
		2       u16     Rate     // [1]
		4       u16     Flags
		6       u8      LowIdx
		7       u8      HighIdx
		8

	 * [1] Where 16384 is 1/60 of a second, 8192 is 1/30, etc.
	*/

	const uint16_t ACTIVE = 0x1;
	const uint16_t REVERSE = 0x2;
	const float TO_SECS = 273.0f + 1.0f/15;
	if (data.len == 8) {
		struct ilbm_crng_array *crng = &desc->crng;
		if (crng->len < ARRAY_LEN(crng->crng)) {
			const uint16_t rate = buf_endian16(data.ptr + 2,
				big_endian);
			const uint16_t flags = buf_endian16(data.ptr + 4,
				big_endian);
			const uint8_t low = data.ptr[6];
			const uint8_t high = data.ptr[7];

			const bool active = (flags & ACTIVE) && rate
				&& low < high;
			crng->crng[crng->len] = (struct ilbm_crng) {
				.secs = TO_SECS/rate,
				.cnt = (uint16_t)(high - low + 1),
				.low = low,
				.active = active,
				.reverse = flags & REVERSE,
			};
			++crng->len;
			crng->active_nr += active;
			img->evolving |= active;
		} else {
			crng->too_many = true;
		}
		return wu_ok;
	}
	return wu_invalid_header;
}

static enum wu_error parse_cmap(struct ilbm_desc *desc, struct wuimg *img,
const struct wuptr data) {
	/* CMAP structure:
		Offset  Size    Name
		0       u8      RGB[len/3][3]
	*/
	(void)img;
	const size_t colors = data.len/3;
	if (desc->pal || desc->planes > 8 || data.len % 3 || colors > 256) {
		return wu_invalid_header;
	}
	desc->colors = (unsigned)colors;
	if (desc->colors) {
		desc->pal = palette_new();
		if (!desc->pal) {
			return wu_alloc_error;
		}
		palette_from_rgb8(desc->pal, data.ptr, colors);
	}
	return wu_ok;
}

static enum wu_error parse_camg(struct ilbm_desc *desc, struct wuimg *img,
const struct wuptr data) {
	/* CAMG structure:
		Offset  Size    Name
		0       u32     Flags
		4

	 * Bits:
		7:  Extra Half-Brite
		11: HAM
	*/
	if (data.len != 4 || desc->format != ilbm_format_ilbm) {
		return wu_invalid_header;
	}
	const uint32_t flags = buf_endian32(data.ptr, big_endian);
	desc->extra_half_brite = flags & 0x80;
	desc->ham = flags & 0x800;
	if (desc->extra_half_brite && desc->planes != 6) {
		return wu_invalid_header;
	}
	if (desc->ham) {
		if (desc->planes < 5 || desc->planes > 8) {
			return wu_invalid_header;
		}
		if (desc->masking) {
			return wu_unsupported_feature;
		}
		img->channels = HAM_CH;
	}
	return wu_ok;
}

static enum wu_error parse_bmhd(struct ilbm_desc *desc, struct wuimg *img,
const struct wuptr data) {
	/* BMHD structure:
		Offset  Size    Name
		0       u16     Width
		2       u16     Height
		4       i16     XOffset
		6       i16     YOffset
		8       u8      Planes
		9       u8      Masking
		10      u8      Compression
		11      u8      _Padding
		12      u16     TransparentColor
		14      u8      XPixelAspect
		15      u8      YPixelAspect
		16      u16     PageWidth
		18      u16     PageHeight
		20
	*/
	if (data.len != 20) {
		return wu_invalid_header;
	}
	img->w = buf_endian16(data.ptr, big_endian);
	img->h = buf_endian16(data.ptr + 2, big_endian);
	img->channels = 1;
	img->bitdepth = 8;
	desc->planes = data.ptr[8];
	desc->masking = data.ptr[9];
	desc->compression = data.ptr[10];
	if (desc->format == ilbm_format_pbm) {
		if (desc->planes != 8) {
			return wu_invalid_header;
		}
		img->align_sh = 1;
	} else {
		switch (desc->planes) {
		case 0: // A colormap-only file
			return wu_ok;
		case 1: case 2: case 3: case 4:
		case 5: case 6: case 7: case 8:
			break;
		case 24: case 32:
			img->channels = 4;
			img->layout = pix_rgba;
			img->alpha = (desc->planes == 24)
				? alpha_ignore : alpha_unassociated;
			break;
		default:
			return wu_invalid_header;
		}
	}

	switch (desc->masking) {
	case ilbm_masking_none: break;
	case ilbm_masking_bitplane:
		if (desc->planes >= 8) {
			return wu_unsupported_feature;
		}
		break;
	case ilbm_masking_value:
		;const uint16_t value = buf_endian16(data.ptr + 12, big_endian);
		if (desc->planes > 8 || value > 255) {
			return wu_unsupported_feature;
		}
		desc->trans_value = (uint8_t)value;
		break;
	default:
		return wu_unsupported_feature;
	}
	switch (desc->compression) {
	case ilbm_compression_none:
	case ilbm_compression_packbits:
		wuimg_aspect_ratio(img, data.ptr[14], data.ptr[15]);
		return wu_ok;
	}
	return wu_samples_wanted;
}

typedef enum wu_error (*chunk_parser_img_t)(struct ilbm_desc *desc,
	struct wuimg *img, struct wuptr data);

typedef enum wu_error (*chunk_parser_other_t)(struct ilbm_desc *desc,
	uint32_t id, struct wuptr data);

enum ilbm_chunk_special {
	ilbm_chunk_bmhd = FOURCC('B', 'M', 'H', 'D'),
	ilbm_chunk_body = FOURCC('B', 'O', 'D', 'Y'),
};

struct ilbm_chunk_def {
	uint32_t id;
	bool essential;
	union {
		chunk_parser_other_t m;
		chunk_parser_img_t img;
	} fn;
};

// Must be kept in ASCII order
static const struct ilbm_chunk_def CHUNK_MAP[] = {
	{FOURCC('(', 'C', ')', ' '), false, .fn.m = parse_text},
	{FOURCC('A', 'N', 'N', 'O'), false, .fn.m = parse_text},
	{FOURCC('A', 'U', 'T', 'H'), false, .fn.m = parse_text},
	{FOURCC('B', 'M', 'H', 'D'), true, .fn.img = parse_bmhd},
	{FOURCC('C', 'A', 'M', 'G'), true, .fn.img = parse_camg},
	{FOURCC('C', 'M', 'A', 'P'), true, .fn.img = parse_cmap},
	{FOURCC('C', 'R', 'N', 'G'), true, .fn.img = parse_crng},
	{FOURCC('D', 'O', 'C', ' '), false, .fn.m = parse_text},
	{FOURCC('F', 'O', 'O', 'T'), false, .fn.m = parse_text},
	{FOURCC('H', 'E', 'A', 'D'), false, .fn.m = parse_text},
	{FOURCC('P', 'A', 'R', 'A'), false, .fn.m = parse_text},
	{FOURCC('P', 'D', 'E', 'F'), false, .fn.m = parse_text},
	{FOURCC('T', 'A', 'B', 'S'), false, .fn.m = parse_text},
	{FOURCC('T', 'E', 'X', 'T'), false, .fn.m = parse_text},
	{FOURCC('T', 'I', 'N', 'Y'), false, .fn.m = parse_tiny},
	{FOURCC('V', 'E', 'R', 'S'), false, .fn.m = parse_text},
};

static int chunk_cmp(const void *k1, const void *k2) {
	const uint32_t *id = k1;
	const struct ilbm_chunk_def *entry = k2;
	return (int)(*id - entry->id);
}

static const struct ilbm_chunk_def * search_chunk_def(const uint32_t id) {
	return bsearch(&id, CHUNK_MAP, ARRAY_LEN(CHUNK_MAP),
		sizeof(*CHUNK_MAP), chunk_cmp);
}

static enum wu_error next_chunk(struct ilbm_desc *desc,
uint32_t *restrict chunk, uint32_t *restrict len, struct wuptr *data) {
	const uint8_t *slice = mp_next_slice(&desc->mp, 8);
	if (slice) {
		*chunk = buf_endian32(slice, big_endian);
		*len = buf_endian32(slice + 4, big_endian);
		*data = mp_next_remaining(&desc->mp, *len);
		if (*len % 2) {
			mp_next_slice(&desc->mp, 1);
		}
		return wu_ok;
	}
	return wu_unexpected_eof;
}

enum wu_error ilbm_parse_footer(struct ilbm_desc *desc) {
	enum wu_error st = wu_ok;
	do {
		uint32_t chunk, len;
		struct wuptr data;
		st = next_chunk(desc, &chunk, &len, &data);
		if (st == wu_unexpected_eof) {
			return wu_ok;
		} else if (st == wu_ok) {
			const struct ilbm_chunk_def *def = search_chunk_def(chunk);
			if (def && !def->essential) {
				st = def->fn.m(desc, chunk, data);
			} else {
				st = desc->callback(chunk, data, desc->usr_ptr);
			}
		}
	} while (st == wu_ok);
	return st;
}

enum wu_error ilbm_parse_header(struct ilbm_desc *desc, struct wuimg *img) {
	// Chunks describing the image must appear between BMHD and BODY.
	bool bmhd_found = false;
	enum wu_error st = wu_ok;
	do {
		uint32_t chunk, len;
		struct wuptr data;
		st = next_chunk(desc, &chunk, &len, &data);
		if (st != wu_ok) {
			if (!desc->planes) { // Colormap only
				return tidy_up(desc, img);
			}
			break;
		}

		if (chunk == ilbm_chunk_body) {
			desc->body = data;
			return tidy_up(desc, img);
		}
		if (data.len < len) {
			return wu_unexpected_eof;
		}
		const struct ilbm_chunk_def *def = search_chunk_def(chunk);
		if (def) {
			if (def->essential) {
				if (chunk == ilbm_chunk_bmhd) {
					if (bmhd_found) {
						return wu_invalid_header;
					}
					bmhd_found = true;
				} else if (!bmhd_found) {
					return wu_invalid_header;
				}
				st = def->fn.img(desc, img, data);
			} else {
				st = def->fn.m(desc, chunk, data);
			}
		} else {
			st = desc->callback(chunk, data, desc->usr_ptr);
		}
	} while (st == wu_ok);
	return st;
}

static enum wu_error dont_callback(uint32_t type, struct wuptr data, void *ptr) {
	(void)type; (void)data; (void)ptr;
	return wu_ok;
}

void ilbm_set_callbacks(struct ilbm_desc *desc, ilbm_callback_t cb,
ilbm_callback_t tcb, void *restrict usr_ptr) {
	desc->callback = cb ? cb : dont_callback;
	desc->text_callback = tcb ? tcb : dont_callback;
	desc->usr_ptr = usr_ptr;
}

enum wu_error ilbm_open(struct ilbm_desc *desc, const struct mparser mp) {
	/* IFF structure:
		Offset  Size    Name
		0       u8      ChunkID[4]   // "FORM" in this case
		4       u32     Len
		8       u8      FormatID[4]  // "ILBM", "PBM ", etc
		12      [Len-4] SubChunks

	 * Subchunk structure:
		Offset  Size    Name
		0       u8      ChunkID[4]
		4       u32     Len
		8       [Len]   Content
		[Len+4]
	*/
	*desc = (struct ilbm_desc) {
		.callback = dont_callback,
		.text_callback = dont_callback,
		.mp = mp,
	};
	const uint8_t *data = mp_next_slice(&desc->mp, 12);
	if (data) {
		if (!memcmp(data, "FORM", 4)) {
			const uint32_t len = buf_endian32(data + 4, big_endian);
			const uint32_t id = buf_endian32(data + 8, big_endian);
			if (len < desc->mp.len - 8) {
				desc->mp.len = (size_t)len + 8;
			}
			switch (id) {
			case ilbm_format_ilbm:
			case ilbm_format_pbm:
				desc->format = id;
				break;
			default:
				return wu_unknown_file_type;
			}
			return wu_ok;
		}
		return wu_unknown_file_type;
	}
	return wu_unexpected_eof;
}

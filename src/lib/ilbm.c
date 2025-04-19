// SPDX-License-Identifier: 0BSD
#include "misc/common.h"
#include "misc/decomp.h"
#include "misc/endian.h"
#include "misc/math.h"
#include "misc/mem.h"
#include "raster/graphics_adapters.h"

#include "lib/ilbm.h"

/* Output channels for HAM. 3 are required, but 4 is much faster as rendering
 * makes heavy use of memcpy */
static const uint8_t HAM_CH = 4;

const char * ilbm_compression_str(const enum ilbm_compression comp) {
	switch (comp) {
	case ilbm_compression_none: return "None";
	case ilbm_compression_packbits: return "PackBits";
	case ilbm_compression_vdat: return "VDAT";
	}
	return "???";
}

void ilbm_cleanup(struct ilbm_desc *desc) {
	palette_unref(desc->pal);
	free(desc->cycle);
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
	return rows;
}

static void expand_ham(const struct ilbm_desc *desc, struct wuimg *img,
const size_t offset) {
	// Use 2-bit HAM for even plane numbers, 1-bit HAM for odds
	const uint8_t color_bits = (uint8_t)((desc->planes - 1) & ~0x1u);
	const uint8_t nc_bits = 8 - color_bits;
	const uint8_t mask = (uint8_t)((1 << color_bits) - 1);
	const uint8_t antimask = (uint8_t)((1 << nc_bits) - 1);

	uint8_t *dst = img->data;
	const uint8_t *src = img->data + offset;
	const struct palette *pal = desc->pal;
	const size_t stride = img->w*HAM_CH;

	for (size_t y = 0; y < img->h; ++y) {
		for (size_t x = 0; x < img->w; ++x) {
			const size_t pix = y*stride + x*HAM_CH;
			const uint8_t s = src[y*img->w + x];
			const uint8_t entry = s & mask;
			const uint8_t ham = s >> color_bits;

			if (ham) {
				if (x) {
					memcpy(dst + pix, dst + pix - HAM_CH,
						HAM_CH);
				} else {
					memcpy(dst + pix, pal->color, HAM_CH);
				}

				// equivalent to:
				//const uint8_t conv[4] = {0 /*unused*/, 2, 0, 1};
				//uint8_t ch = conv[ham];
				uint8_t ch = (ham ^ 2);
				ch ^= ch >> 1;

				dst[pix + ch] = (uint8_t)(entry << nc_bits)
					| (dst[pix + ch] & antimask);
			} else {
				memcpy(dst + pix, pal->color + entry, HAM_CH);
			}
		}
	}
}

static struct wu_st expand_body(const struct ilbm_desc *desc, struct wuimg *img,
const struct wuptr body) {
	size_t offset = 0;
	if (desc->ham) {
		offset = wuimg_size(img) - img->w*img->h;
	}
	const size_t w = interleave_bitplanes(desc, img, img->data + offset,
		body, desc->planes);
	if (desc->ham) {
		expand_ham(desc, img, offset);
	}
	return wuerr_partial(w, img->h);
}

static struct wu_st decompress_ilbm(const struct ilbm_desc *desc,
struct wuimg *img, const struct wuptr body) {
	const size_t upack_len = strip_length(img->w, 1, 1) * desc->planes * img->h;
	uint8_t *upack = malloc(upack_len);
	if (upack) {
		const size_t w = decomp_packbits(upack, upack_len,
			(const int8_t *)body.ptr, body.len);
		expand_body(desc, img, wuptr_mem(upack, w));
		free(upack);
		return wuerr_partial(w, upack_len);
	}
	return WUERR_HERE(wu_alloc_error);
}

static void unscramble_vdat(uint16_t *restrict dst, const uint16_t *restrict src,
const size_t plane_stride, const size_t h, const uint8_t planes) {
	for (size_t y = 0; y < h; ++y) {
		for (uint8_t z = 0; z < planes; ++z) {
			for (size_t x = 0; x < plane_stride; ++x) {
				dst[y*plane_stride*planes + z*plane_stride + x] =
					src[z*plane_stride*h + x*h + y];
			}
		}
	}
}

static bool next_vdat(struct mparser *mp, struct wuptr *body) {
	struct iff_chunk chunk = {0};
	const uint8_t *data = mp_slice(mp, sizeof(chunk));
	if (data) {
		memcpy(&chunk, data, sizeof(chunk));
		*body = mp_avail(mp, endian32(chunk.len, big_endian));
		const uint32_t vdat = FOURCC('V', 'D', 'A', 'T');
		return endian32(chunk.id, big_endian) == vdat && body->len > 2;
	}
	return false;
}

static size_t decomp_vdat(uint16_t *restrict dst, const size_t dst_len,
const uint8_t *restrict src, const size_t src_len) {
	/* VDAT chunk:
		Offset  Type    Name
		0       u16     DataOffset
		4       s8      Ctrl[]
		DataOff u16     Data[]
		ChunkLen

	 * There seem to be as many VDAT chunks as there are bitplanes.
	*/
	struct mparser mp = mp_mem(src_len, src);
	size_t d = 0;
	struct wuptr body;
	while (next_vdat(&mp, &body)) {
		const uint16_t data_off = buf_endian16(body.ptr, big_endian);
		size_t data_pos = data_off;
		for (size_t s = 2; s < data_off; ++s) {
			const int8_t c = (int8_t)body.ptr[s];
			size_t run;
			bool repeat;
			if (c < 0) {
				run = (size_t)-c;
				repeat = false;
			} else if (c > 1) {
				run = (size_t)c;
				repeat = true;
			} else {
				if (data_pos + 2 > body.len) {
					break;
				}
				run = buf_endian16(body.ptr + data_pos, big_endian);
				data_pos += 2;
				repeat = (bool)c;
			}
			if (d + run > dst_len) {
				return d;
			}
			if (repeat) {
				if (data_pos + 2 > body.len) {
					break;
				}
				memset16(dst + d, body.ptr + data_pos, run);
				data_pos += 2;
			} else {
				if (data_pos + run*2 > body.len) {
					break;
				}
				memcpy(dst + d, body.ptr + data_pos, run*2);
				data_pos += run*2;
			}
			d += run;
		}
	}
	return d;
}

static struct wu_st decomp_vertical_rle(const struct ilbm_desc *desc,
struct wuimg *img, const struct wuptr body) {
	const size_t stride = strip_length(img->w, 1, 1);
	const size_t upack_len = stride * desc->planes * img->h;
	uint16_t *upack = malloc(upack_len*2);
	if (upack) {
		size_t w = decomp_vdat(upack, upack_len/2, body.ptr, body.len);
		uint16_t *linear = upack + upack_len/2;
		unscramble_vdat(linear, upack, stride/2, img->h,
			desc->planes);
		expand_body(desc, img, wuptr_mem(linear, upack_len));
		free(upack);
		return wuerr_partial(w, upack_len/2);
	}
	return WUERR_HERE(wu_alloc_error);
}

static struct wu_st ilbm_decode(const struct ilbm_desc *desc, struct wuimg *img,
const struct wuptr body) {
	if (desc->planes == 0) {
		return wuok();
	}
	if (!wuimg_alloc_noverify(img)) {
		return WUERR_HERE(wu_alloc_error);
	}
	switch (desc->format) {
	case ilbm_format_ilbm:
		switch (desc->compression) {
		case ilbm_compression_none:
			return expand_body(desc, img, body);
		case ilbm_compression_packbits:
			return decompress_ilbm(desc, img, body);
		case ilbm_compression_vdat:
			return decomp_vertical_rle(desc, img, body);
		}
		break;
	case ilbm_format_pbm:
		;const size_t size = wuimg_size(img);
		switch (desc->compression) {
		case ilbm_compression_none:
			;const size_t cpy = zumin(size, body.len);
			memcpy(img->data, body.ptr, cpy);
			return wuerr_partial(cpy, size);
		case ilbm_compression_packbits:
			return wuerr_partial(decomp_packbits(img->data, size,
				(const int8_t *)body.ptr, body.len), size);
		case ilbm_compression_vdat:
			break;
		}
		break;
	}
	return WUERR_HERE(wu_invalid_params);
}

struct wu_st ilbm_decode_tiny(const struct ilbm_desc *desc, struct wuimg *main,
struct wuimg *tiny) {
	if (desc->tiny.present && wuimg_clone(tiny, main)) {
		tiny->w = desc->tiny.w;
		tiny->h = desc->tiny.h;
		struct wu_st st = wuimg_verify_st(tiny);
		if (wu_isok(st)) {
			if (tiny->mode == image_mode_palette && desc->cycle) {
				memcpy(tiny->u.palette->color, desc->cycle->color,
					sizeof(desc->cycle->color));
			}
			return ilbm_decode(desc, tiny, desc->tiny.data);
		}
		return st;
	}
	return WUERR_HERE(wu_decoding_error);
}

struct wu_st ilbm_decode_main(const struct ilbm_desc *desc, struct wuimg *img) {
	return ilbm_decode(desc, img, desc->body);
}

static struct wu_st tidy_up(struct ilbm_desc *desc, struct wuimg *img) {
	if (desc->planes == 0) {
		if (desc->colors == 0) {
			return wuerr(wu_no_image_data, NULL);
		}
		img->data = (uint8_t *)desc->pal;
		memmove(img->data, desc->pal->color, sizeof(desc->pal->color));
		desc->pal = NULL;
		img->w = desc->colors;
		img->h = 1;
		img->channels = 4;
		img->bitdepth = 8;
		img->evolving = false;
	} else if (desc->pal) {
		if (desc->ham) {
			img->channels = HAM_CH;
			img->alpha = alpha_ignore;
			if (desc->cycle) {
				// TODO
				// Sample: AH_Swimmer.iff
				free(desc->cycle);
				desc->cycle = NULL;
			}
		} else {
			img->channels = 1;
			struct palette *pal = desc->pal;
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
			wuimg_palette_set(img, palette_ref(pal));
			if (desc->cycle) {
				palette_cycle_set(desc->cycle, pal);
			}
		}
	} else {
		if (desc->ham || desc->cycle) {
			return wuerr(wu_invalid_header,
				"HAM or CRNG with non-paletted image");
		}
		if (desc->planes > 8) {
			img->channels = 4;
			img->bitrange = 8;
		} else {
			img->channels = 1;
			img->bitrange = desc->planes;
		}
	}
	return wuimg_verify_st(img);
}

static struct wu_st finish_chunk(struct ilbm_desc *desc,
const struct iff_chunk chunk, const char *msg) {
	mp_seek_cur(&desc->mp, iff_chunk_padding(chunk));
	return wuerr(wu_ok, msg);
}

static struct wu_st body_stop(struct iff_state *iff, void *ptr,
const struct iff_chunk chunk) {
	(void)iff;
	struct ilbm_desc *desc = ptr;
	desc->body = mp_avail(&desc->mp, chunk.len);
	mp_seek_cur(&desc->mp, iff_chunk_padding(chunk));
	return wuerr(wu_no_change, NULL);
}

static struct wu_st parse_tiny(struct iff_state *iff, void *ptr,
const struct iff_chunk chunk) {
	/* TINY structure:
		Offset  Size    Name
		0       u16     Width
		2       u16     Height
		4       [Len-4] Data
	 * Data is compressed just like the main image.
	*/
	(void)iff;
	struct ilbm_desc *desc = ptr;
	const char *msg = "unusable TINY chunk";
	if (chunk.len > 4 && desc->planes) {
		/* TODO: Find non-PBM samples with TINY.
		 * FONA.LBM has a thumbnail, but has 0 height and unknown
		 * data.
		 * Maybe also find a source that's not a mysterious edit in
		 * Wikipedia. */
		const struct wuptr data = mp_avail(&desc->mp, chunk.len);
		uint16_t w = buf_endian16(data.ptr, big_endian);
		uint16_t h = buf_endian16(data.ptr + 2, big_endian);
		desc->tiny = (struct ilbm_tiny) {
			.present = w && h,
			.w = w, .h = h,
			.data = wuptr_mem(data.ptr + 4, data.len - 4),
		};
		msg = NULL;
	}
	return finish_chunk(desc, chunk, msg);
}

static struct wu_st parse_crng(struct iff_state *iff, void *ptr,
const struct iff_chunk chunk) {
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
	(void)iff;
	struct ilbm_desc *desc = ptr;
	const uint8_t *data = mp_slice(&desc->mp, chunk.len);
	if (!data) {
		return WUERR_HERE(wu_unexpected_eof);
	}
	const char *msg = "CRNG length != 8";
	if (chunk.len == 8) {
		msg = NULL;
		const uint8_t MAX_SLOTS = 16;
		struct palette_cycle *cycle = desc->cycle;
		if (!cycle) {
			cycle = palette_cycle_new(MAX_SLOTS);
			if (!cycle) {
				return WUERR_HERE(wu_alloc_error);
			}
			desc->cycle = cycle;
		}

		if (cycle->len < cycle->alloc) {
			const uint16_t ACTIVE = 0x1;
			const uint16_t REVERSE = 0x2;
			const float TO_SECS = 273.0f + 1.0f/15;
			const uint16_t rate = buf_endian16(data + 2,
				big_endian);
			const uint16_t flags = buf_endian16(data + 4,
				big_endian);
			const uint8_t lo = data[6];
			const uint8_t hi = data[7];

			const bool active = (flags & ACTIVE) && rate && lo < hi;
			cycle->crng[cycle->len] = (struct palette_crng) {
				.lo = lo,
				.hi = hi,
				.active = active,
				.reverse = flags & REVERSE,
				.secs = TO_SECS/rate,
			};
			++cycle->len;
			cycle->active_nr += active;
			desc->img->evolving |= active;
		} else {
			cycle->too_many = true;
		}
	}
	return finish_chunk(desc, chunk, msg);
}

static struct wu_st parse_cmap(struct iff_state *iff, void *ptr,
const struct iff_chunk chunk) {
	/* CMAP structure:
		Offset  Size    Name
		0       u8      RGB[len/3][3]
	*/
	(void)iff;
	struct ilbm_desc *desc = ptr;
	const unsigned colors = chunk.len/3;
	const char *msg = NULL;
	if (desc->pal) {
		msg = "repeated CMAP";
	} else if (desc->planes > 8) {
		msg = "CMAP with depth > 8";
	} else if (chunk.len % 3) {
		msg = "CMAP length not divisible by 3";
	} else if (colors > 256) {
		msg = "CMAP has more than 256 colors";
	}
	if (msg) {
		return wuerr(wu_invalid_header, msg);
	}
	desc->colors = colors;
	if (desc->colors) {
		const uint8_t *data = mp_slice(&desc->mp, chunk.len);
		if (!data) {
			return WUERR_HERE(wu_unexpected_eof);
		}
		desc->pal = palette_new();
		if (!desc->pal) {
			return WUERR_HERE(wu_alloc_error);
		}
		palette_from_rgb8(desc->pal, data, colors);
	}
	return finish_chunk(desc, chunk, NULL);
}

static struct wu_st parse_camg(struct iff_state *iff, void *ptr,
const struct iff_chunk chunk) {
	/* CAMG structure:
		Offset  Size    Name
		0       u32     Flags
		4

	 * Bits:
		7:  Extra Half-Brite
		11: HAM
	*/
	(void)iff;
	struct ilbm_desc *desc = ptr;
	if (chunk.len != 4) {
		return wuerr(wu_invalid_header, "unexpected CAMG size");
	} else if (desc->format != ilbm_format_ilbm) {
		return wuerr(wu_unsupported_feature, "CAMG with non-ILBM image");
	}
	const uint8_t *data = mp_slice(&desc->mp, chunk.len);
	if (!data) {
		return WUERR_HERE(wu_unexpected_eof);
	}
	const uint32_t flags = buf_endian32(data, big_endian);
	desc->extra_half_brite = flags & 0x80;
	desc->ham = flags & 0x800;
	if (desc->extra_half_brite && desc->planes != 6) {
		return wuerr(wu_invalid_header,
			"Extra Half-Brite with planes != 6");
	}
	if (desc->ham) {
		if (desc->planes < 5 || desc->planes > 8) {
			return wuerr(wu_invalid_header,
				"HAM with planes < 5 or > 8");
		}
		if (desc->masking) {
			return wuerr(wu_unsupported_feature,
				"HAM mode with masking");
		}
	}
	return finish_chunk(desc, chunk, NULL);
}

static struct wu_st parse_bmhd(struct iff_state *iff, struct ilbm_desc *desc,
const struct iff_chunk chunk) {
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
	(void)iff;
	if (chunk.len != 20) {
		return wuerr(wu_invalid_header, "BMHD with length != 20");
	}
	const uint8_t *data = mp_slice(&desc->mp, chunk.len);
	if (!data) {
		return WUERR_HERE(wu_unexpected_eof);
	}
	struct wuimg *img = desc->img;
	img->w = buf_endian16(data, big_endian);
	img->h = buf_endian16(data + 2, big_endian);
	img->bitdepth = 8;
	desc->planes = data[8];
	desc->masking = data[9];
	desc->compression = data[10];
	if (desc->format == ilbm_format_pbm) {
		if (desc->planes != 8) {
			return wuerr(wu_invalid_header, "PBM with depth != 8");
		}
		img->align_sh = 1;
	} else {
		switch (desc->planes) {
		case 0: // A colormap-only file
			return finish_chunk(desc, chunk, NULL);
		case 1: case 2: case 3: case 4:
		case 5: case 6: case 7: case 8:
			break;
		case 24: case 32:
			img->layout = which_end() == little_endian
				? pix_rgba : pix_abgr;
			img->alpha = (desc->planes == 24)
				? alpha_ignore : alpha_unassociated;
			break;
		default:
			return wuerr(wu_invalid_header, "unexpected depth");
		}
	}

	switch (desc->masking) {
	case ilbm_masking_none: break;
	case ilbm_masking_bitplane:
		if (desc->planes >= 8) {
			return wuerr(wu_unsupported_feature,
				"mask plane with depth >= 8");
		}
		break;
	case ilbm_masking_value:
		;const uint16_t value = buf_endian16(data + 12, big_endian);
		if (desc->planes > 8 || value > 255) {
			return wuerr(wu_unsupported_feature,
				"mask value > 255 or with depth > 8");
		}
		desc->trans_value = (uint8_t)value;
		break;
	default:
		return wuerr(wu_unsupported_feature, "unknown masking method");
	}
	switch (desc->compression) {
	case ilbm_compression_vdat:
		if (desc->format != ilbm_format_ilbm) {
			return wuerr(wu_uncertain_validity,
				"VDAT compression with non-ILBM format");
		}
		// fallthrough
	case ilbm_compression_none:
	case ilbm_compression_packbits:
		wuimg_aspect_ratio(img, data[14], data[15]);
		return finish_chunk(desc, chunk, NULL);
	}
	return wuerr(wu_uncertain_validity, "unknown compression method");
}

static const struct iff_table CHUNK_MAP[] = {
	{FOURCC('B', 'O', 'D', 'Y'), body_stop},
	{FOURCC('C', 'A', 'M', 'G'), parse_camg},
	{FOURCC('C', 'M', 'A', 'P'), parse_cmap},
	{FOURCC('C', 'R', 'N', 'G'), parse_crng},
	{FOURCC('T', 'I', 'N', 'Y'), parse_tiny},
};

static struct wu_st start_ilbm(struct iff_state *iff, void *ptr,
const struct iff_chunk chunk) {
	iff->table = CHUNK_MAP;
	iff->table_len = ARRAY_LEN(CHUNK_MAP);
	return parse_bmhd(iff, ptr, chunk);
}

static const struct iff_table START_TABLE[] = {
	{FOURCC('B', 'M', 'H', 'D'), start_ilbm},
};

static struct wu_st ilbm_fallback(struct iff_state *iff, void *ptr,
const struct iff_chunk chunk) {
	(void)iff;
	struct ilbm_desc *desc = ptr;
	if (desc->callback) {
		const struct wuptr data = mp_avail(&desc->mp, chunk.len);
		desc->callback(desc->usr_ptr, chunk, data);
	} else {
		mp_seek_cur(&desc->mp, chunk.len);
	}
	return finish_chunk(desc, chunk, NULL);
}

static struct wu_st ilbm_parse(struct ilbm_desc *desc,
const struct iff_table *table, const unsigned table_len) {
	struct iff_state iff = {
		.table = table,
		.table_len = table_len,
		.endian = big_endian,
		.fallback = ilbm_fallback,
		.user = desc,
	};
	struct wu_st st;
	do {
		st = iff_next_mparser(&iff, &desc->mp, (struct iff_chunk){0});
	} while (wu_isok(st));
	return st;
}

struct wu_st ilbm_parse_footer(struct ilbm_desc *desc) {
	ilbm_parse(desc, NULL, 0);
	return wuok();
}

struct wu_st ilbm_parse_header(struct ilbm_desc *desc, struct wuimg *img) {
	desc->img = img;
	struct wu_st st = ilbm_parse(desc, START_TABLE, ARRAY_LEN(START_TABLE));
	if (st.st == wu_no_change) {
		return tidy_up(desc, img);
	}
	return st;
}

void ilbm_set_callbacks(struct ilbm_desc *desc, ilbm_callback_t cb,
void *restrict usr_ptr) {
	desc->callback = cb;
	desc->usr_ptr = usr_ptr;
}

struct wu_st ilbm_open(struct ilbm_desc *desc, const struct wuptr mem) {
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
		.mp = mp_wuptr(mem),
	};
	const uint8_t *data = mp_slice(&desc->mp, 12);
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
				return wuok();
			}
			return wuerr(wu_unknown_file_type,
				"unknown IFF image format");
		}
		return WUERR_HERE(wu_unknown_file_type);
	}
	return WUERR_HERE(wu_unexpected_eof);
}

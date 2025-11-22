// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2022 kaleido
#include <stdlib.h>
#include <string.h>

#include "misc/bit.h"
#include "misc/common.h"
#include "misc/endian.h"
#include "misc/mem.h"
#include "raster/fmt.h"
#include "raster/strip.h"
#include "wpx.h"

/* WPX common header (after signature):
	Offset  Size    Name
	0       DWORD   ???
	4       BYTE    Version // Always 1
	5       BYTE    ???
	6       BYTE    DirCount
	7       BYTE    DirSize
	8       STRUCT  Dir[DirCount]

 * Dir struct:
	Offset  SIZE    Name
	0       BYTE    ID
	1       BYTE    CompressionFlags
	2       WORD    Padding
	4       DWORD   FileOffset
	8       DWORD   DecompressedSize
	12      DWORD   CompressedSize
	16
*/

static const size_t TABLE_DIM = 256;

enum wpx_ts_type {
	wpx_ts_single_byte,
	wpx_ts_mixed,
	wpx_ts_stream,
	wpx_ts_offset,
};

enum wpx_rt_type {
	wpx_rt_dict,
	wpx_rt_table,
	wpx_rt_stream,
};

struct wpx_retriever {
	enum wpx_rt_type type;
	uint8_t *table;
	uint8_t *dict;
};

struct wpx_transcriptor {
	enum wpx_ts_type type;
	uint8_t base_size;
	size_t off[8];
};

struct bit_and_byte_reader {
	const uint8_t *mem;
	size_t len;
	size_t pos;
	int bitpos;
	uint8_t byte;
};

void wpx_bmp_cleanup(struct wpx_bmp_desc *desc) {
	free(desc->dir.sections);
	palette_unref(desc->pal);
}

static void free_list(struct wpx_ia2_list *list) {
	free(list->str);
	free(list->off);
}

void wpx_ia2_cleanup(struct wpx_ia2_desc *desc) {
	free_list(&desc->names);
	free_list(&desc->sfx);
	free(desc->mys5);
	free(desc->range);
	free(desc->geom);
	free(desc->frames);
	free(desc->dir.sections);
}

static void free_retriever(struct wpx_retriever *rt) {
	if (rt->type != wpx_rt_stream) {
		free(rt->table);
	}
}

static uint8_t next_byte(struct bit_and_byte_reader *reader) {
	uint8_t byte = 0;
	if (reader->pos < reader->len) {
		byte = reader->mem[reader->pos];
		++reader->pos;
	}
	return byte;
}

static uint16_t next_word(struct bit_and_byte_reader *reader) {
	uint16_t word = 0;
	if (reader->len - reader->pos >= 2) {
		word = buf_endian16(reader->mem + reader->pos, little_endian);
		reader->pos += 2;
	}
	return word;
}

static bool next_bit(struct bit_and_byte_reader *reader) {
	if (reader->bitpos < 0) {
		reader->byte = next_byte(reader);
		reader->bitpos = 7;
	}
	const bool bit = (reader->byte >> reader->bitpos) & 1;
	--reader->bitpos;
	return bit;
}

static size_t next_bits(struct bit_and_byte_reader *reader, const int32_t n) {
	size_t bits = 0;
	for (int32_t x = 0; x < n; ++x) {
		bits = (bits << 1) | next_bit(reader);
	}
	return bits;
}

static void init_dict(uint8_t *dict) {
	for (int y = 0; y < (int)TABLE_DIM; ++y) {
		for (int x = 0; x < (int)TABLE_DIM; ++x) {
			dict[(int)TABLE_DIM*y + x] = (uint8_t)(-1 - y - x);
		}
	}
}

static bool init_table(uint8_t *restrict table,
const uint8_t *restrict bitstream, size_t *bitpos, size_t src_len) {
	*bitpos = TABLE_DIM/2 * 8;
	src_len *= 8;
	if (*bitpos >= src_len) {
		return false;
	}
	for (size_t n = 0; n < TABLE_DIM; ++n) {
		const uint8_t size = (bitstream[n/2] >> ((n & 1) * 4))
			& 0x0f;
		if (size) {
			if (src_len - *bitpos <= size) {
				return false;
			}
			const uint32_t idx = bit_advn(bitstream,
				bitpos, size) << (16 - size);
			table[idx] = size;
			table[idx + 1] = (uint8_t)n;
		}
	}
	return true;
}

static bool init_retriever(struct wpx_retriever *rt, const uint8_t fmt,
const uint8_t *bitstream, size_t *bitpos, const size_t src_len) {
	if (fmt & 4) {
		rt->type = wpx_rt_dict;
	} else if (fmt & 2) {
		rt->type = wpx_rt_table;
	} else {
		rt->type = wpx_rt_stream;
		return true;
	}
	const size_t dims = TABLE_DIM * TABLE_DIM;
	const size_t total = dims * (rt->type == wpx_rt_dict ? 2 : 1);
	rt->table = malloc(total);
	if (!rt->table) {
		return false;
	}
	if (!init_table(rt->table, bitstream, bitpos, src_len)) {
		free(rt->table);
		return false;
	}
	if (rt->type == wpx_rt_dict) {
		rt->dict = rt->table + dims;
		init_dict(rt->dict);
	}
	return true;
}

static bool init_transcriptor(struct wpx_transcriptor *ts, const uint8_t fmt,
const uint8_t qs, const size_t stride) {
	if (stride) {
		if (fmt & 1) {
			if (fmt & 8) {
				ts->type = wpx_ts_single_byte;
			} else {
				ts->type = wpx_ts_mixed;
			}
		} else {
			ts->type = wpx_ts_offset;
		}
		ts->base_size = (qs == 1) ? 2 : 1;
		for (size_t i = 0; i < ARRAY_LEN(ts->off); ++i) {
			if (i < 3 || stride < qs*4) {
				ts->off[i] = qs * (i+1);
			} else if (i < ARRAY_LEN(ts->off) - 1) {
				ts->off[i] = stride - qs + qs * (i-3);
			} else {
				ts->off[i] = stride * 2;
			}
		}
	} else {
		if (fmt & 8) {
			return false;
		}
		ts->type = wpx_ts_stream;
	}
	return true;
}

static uint8_t table_binary_search(struct wpx_retriever *rt,
struct bit_and_byte_reader *reader) {
	int idx = 0;
	for (int n = 15; n; --n) {
		idx |= next_bit(reader) << n;
		if (rt->table[idx] == 16 - n) {
			return rt->table[idx + 1];
		}
	}
	return 0; // Shouldn't happen
}

static uint8_t retriever_byte(struct wpx_retriever *rt,
struct bit_and_byte_reader *reader, const uint8_t *restrict dst) {
	uint8_t byte = 0;
	switch (rt->type) {
	case wpx_rt_dict:
		byte = memcycle(rt->dict + (*dst << 8),
			table_binary_search(rt, reader));
		break;
	case wpx_rt_table:
		byte = table_binary_search(rt, reader);
		break;
	case wpx_rt_stream:
		byte = next_byte(reader);
		break;
	}
	return byte;
}

static void transcriptor_loc(const struct wpx_transcriptor *ts,
struct bit_and_byte_reader *reader, size_t *offset, size_t *count) {
	// Oh dear
	switch (ts->type) {
	case wpx_ts_single_byte:
		if (next_bit(reader)) {
			goto from_byte;
		}
		goto from_offsets;
	case wpx_ts_mixed:
		if (next_bit(reader)) {
	case wpx_ts_stream:
			if (next_bit(reader)) {
from_byte:
				*offset = next_byte(reader) + 1;
				*count = 2;
			} else {
				*offset = next_word(reader) + 1;
				*count = 3;
			}
			break;
		}
		// fallthrough
	case wpx_ts_offset:
from_offsets:
		*offset = ts->off[next_bits(reader, 3)];
		*count = ts->base_size;
		break;
	}

	int32_t n = 0;
	while (n < 31 && !next_bit(reader)) {
		++n;
	}
	*count += ((1u << n) | next_bits(reader, n)) - 1;
}

static size_t decode_section_data(const struct wpx_section *section,
uint8_t *restrict dst, const size_t dst_len, const uint8_t *restrict src,
size_t src_len, const uint8_t quant_size, const size_t stride) {
	const size_t start_stride = strip_length(quant_size, 8, 2);
	if (start_stride >= src_len || quant_size >= dst_len) {
		return 0;
	}

	memcpy(dst, src, quant_size);
	src += start_stride;
	src_len -= start_stride;
	size_t d = quant_size;
	size_t bitpos = 0;

	struct wpx_transcriptor ts = {0};
	if (!init_transcriptor(&ts, section->fmt, quant_size, stride)) {
		return 0;
	}
	struct wpx_retriever rt = {0};
	if (!init_retriever(&rt, section->fmt, src, &bitpos, src_len)) {
		return 0;
	}
	struct bit_and_byte_reader reader = {
		.mem = src,
		.len = src_len,
		.pos = bitpos/8 + 1,
		.bitpos = 7 - (int)bitpos%8,
		.byte = src[bitpos/8],
	};
	while (d < dst_len && reader.pos < reader.len) {
		if (next_bit(&reader)) {
			dst[d] = retriever_byte(&rt, &reader, dst + d - quant_size);
			++d;
		} else {
			size_t offset = 0;
			size_t count = 0;
			transcriptor_loc(&ts, &reader, &offset, &count);
			if (dst_len - d < count) {
				break;
			} else if (offset <= d) {
				memrepeat(dst, d, offset, count);
				d += count;
			}
		}
	}
	free_retriever(&rt);
	return d;
}

static bool section_is_uncompressed(const struct wpx_section *section) {
	return section->fmt == 0x80 || !section->comp_size;
}

static size_t get_section_data(const struct mparser *mp,
const struct wpx_section *section, void *restrict dst, const size_t dst_len,
const uint8_t quant_size, const size_t stride) {
	struct wuptr src;
	if (section_is_uncompressed(section)) {
		src = mp_avail_at(mp, section->offset, dst_len);
		memcpy(dst, src.ptr, src.len);
		return src.len;
	} else if (!quant_size) {
		return 0;
	}
	src = mp_avail_at(mp, section->offset, section->comp_size);
	return decode_section_data(section, dst, dst_len, src.ptr, src.len,
		quant_size, stride);
}

struct wu_st wpx_bmp_decode(const struct wpx_bmp_desc *desc, struct wuimg *img) {
	if (!wuimg_alloc_noverify(img)) {
		return WUERR_HERE(wu_alloc_error);
	}

	const bool will_sew = desc->mask_idx >= 0;
	const uint8_t ch = (uint8_t)(desc->depth / 8);
	struct sewing_machine sew;
	strip_sew_init(&sew, img->data, desc->pal, img->w, img->h, ch,
		img->align_sh, will_sew);

	size_t expect = sew.color.len;
	size_t written = get_section_data(&desc->mp,
		desc->dir.sections + desc->raster_idx, sew.color.ptr,
		sew.color.len, ch, sew.color.stride);

	if (will_sew) {
		if (!strip_sew_alloc_alpha(&sew)) {
			return WUERR_HERE(wu_alloc_error);
		}
		expect += sew.alpha.len;
		written += get_section_data(&desc->mp,
			desc->dir.sections + desc->mask_idx, sew.alpha.ptr,
			sew.alpha.len, 1, sew.alpha.stride);

		strip_sew_alpha(&sew);
		strip_sew_free_alpha(&sew);
	}
	return wuerr_partial(written, expect);
}

static struct wu_st read_palette(struct wpx_bmp_desc *desc,
const struct wpx_section *section) {
	const size_t size = section->decomp_size;
	if (size % 3 || size > 256*3) {
		return wuerr(wu_invalid_header,
			"palette not divisible by 3 or with too many entries");
	}

	desc->pal = palette_new();
	if (!desc->pal) {
		return WUERR_HERE(wu_alloc_error);
	}

	const size_t elems = size/3;
	uint8_t *buf = (uint8_t *)desc->pal->color + (4 - 3)*elems;
	const size_t read = get_section_data(&desc->mp, section, buf, size, 3, 48);
	if (read != size) {
		return WUERR_HERE(wu_unexpected_eof);
	}
	palette_from_rgb8(desc->pal, buf, elems);
	return WU_OK;
}

static struct wu_st read_metadata(struct wpx_bmp_desc *desc,
struct wuimg *img, const struct wpx_section *section) {
	uint8_t metadata[16];
	if (get_section_data(&desc->mp, section, metadata, sizeof(metadata), 1, 0)
	!= sizeof(metadata)) {
		return WUERR_HERE(wu_unexpected_eof);
	}

	desc->depth = metadata[0x0c];
	switch (desc->depth) {
	case 8: case 24: case 32:
		break;
	default:
		return wuerr(wu_invalid_header, "depth is not 8, 24, or 32");
	}
	img->w = buf_endian16(metadata + 4, little_endian);
	img->h = buf_endian16(metadata + 6, little_endian);
	img->channels = (uint8_t)(desc->depth / 8);
	img->bitdepth = 8;
	wuimg_align(img, 4);
	img->layout = (desc->depth == 8) ? pix_rgba : pix_bgra;
	return WU_OK;
}

static struct wpx_section * process_section(struct wpx_section *s) {
	s->offset = endian32(s->offset, little_endian);
	s->decomp_size = endian32(s->decomp_size, little_endian);
	s->comp_size = endian32(s->comp_size, little_endian);
	return s;
}

static struct wu_st load_section_dir(struct wpx_dir *dir,
struct mparser *mp, const uint8_t min_sections) {
	/* WPX BMP/IA2 struct (after magic bytes):
		Offset  Type    Size
		0       u32     DirOffset
		4       u16     Version
		6       u8      DirCount
		7       u8      DirSize
		8
	*/

	const uint8_t *buf = mp_slice(mp, 8);
	if (buf) {
		const size_t dirsize = sizeof(*dir->sections);
		dir->count = buf[6];
		if (buf_endian16l(buf+4) == 1 && dir->count >= min_sections
		&& buf[7] == dirsize) {
			const size_t dir_len = dir->count * dirsize;
			mp_seek_cur(mp, (ptrdiff_t)buf_endian32l(buf) - 0x10);
			buf = mp_slice(mp, dir_len);
			if (buf) {
				dir->sections = memdup(buf, dir_len);
				return dir->sections
					? WU_OK
					: WUERR_HERE(wu_alloc_error);
			}
			return WUERR_HERE(wu_unexpected_eof);
		}
		return wuerr(wu_invalid_header, "bad section dir params");
	}
	return WUERR_HERE(wu_unexpected_eof);
}

struct wu_st wpx_bmp_parse(struct wpx_bmp_desc *desc, const struct wuptr mem,
struct wuimg *img) {
	*desc = (struct wpx_bmp_desc) {
		.mp = mp_wuptr(mem),
		.raster_idx = -1,
		.mask_idx = -1,
	};
	const uint8_t sig[] = {'W', 'P', 'X', 0x1a, 'B', 'M', 'P', 0};
	enum wu_error e = fmt_sigcmp_mem(sig, sizeof(sig), &desc->mp);
	if (e != wu_ok) {
		return WUERR_HERE(e);
	}

	struct wu_st st = load_section_dir(&desc->dir, &desc->mp, 2);
	if (!wu_isok(st)) {
		return st;
	}

	int info_idx = -1;
	for (uint8_t i = 0; i < desc->dir.count; ++i) {
		const struct wpx_section *s = process_section(
			desc->dir.sections + i);
		switch (s->id) {
		case wpx_bmp_info:
			info_idx = i;
			st = read_metadata(desc, img, s);
			break;
		case wpx_bmp_data:
			desc->raster_idx = i;
			break;
		case wpx_bmp_palette:
			st = read_palette(desc, s);
			break;
		case wpx_bmp_mask:
			desc->mask_idx = i;
			break;
		default:
			return wuerr(wu_invalid_header, "unknown section id");
		}
		if (!wu_isok(st)) {
			return st;
		}
	}
	if (info_idx < 0 || desc->raster_idx < 0) {
		return wuerr(wu_invalid_header, "no image found");
	}
	if (desc->mask_idx >= 0) {
		if (desc->depth == 8 && !desc->pal) {
			// Is this possible?
			return wuerr(wu_uncertain_validity,
				"8-bit grayscale with alpha mask");
		}
		img->channels = 4;
	} else if (desc->pal) {
		if (img->channels != 1) {
			return wuerr(wu_invalid_header,
				"multi-channel image with palette");
		}
		wuimg_palette_set(img, desc->pal);
		desc->pal = NULL;
	}
	return wuimg_verify_st(img);
}


struct wuptr wpx_ia2_get_frame(struct wpx_ia2_desc *desc, const uint32_t i) {
	const size_t pos = desc->base + desc->frames[i];
	mp_seek_set(&desc->mp, pos);
	return mp_remaining(&desc->mp);
}

bool wpx_ia2_list_get(const struct wpx_ia2_list *list, const uint32_t idx,
struct wuptr *str) {
	const uint32_t pos = list->off[idx];
	if (pos < list->str_len) {
		const char *s = list->str + pos;
		str->ptr = (const uint8_t *)s;
		str->len = strnlen(s, list->str_len - pos);
		return true;
	}
	return false;
}

static struct wu_st alloc_section_data(struct wpx_ia2_desc *desc,
const struct wpx_section *section, void *restrict where, const size_t size) {
	void **w = where;
	/* Reject any section whose compressed size is more than twice the
	 * uncompressed size, or has been previously allocated. */
	if (section->comp_size/2 < size && *w == NULL) {
		uint8_t *dst = small_malloc(size, 1);
		if (dst) {
			const size_t written = get_section_data(&desc->mp,
				section, dst, size, 1, 0);
			if (written == size) {
				*w = dst;
				return WU_OK;
			}
			free(dst);
			return WUERR_HERE(wu_unexpected_eof);
		}
		return WUERR_HERE(wu_alloc_error);
	}
	return wuerr(wu_invalid_header,
		"compressed size exceeds decompressed size, or section is repeated");
}

static void struct_swap(void *ptr, const size_t bytes) {
	uint32_t *arr = ptr;
	endian_loop32(arr, little_endian, bytes / sizeof(*arr));
}

static struct wu_st read_array(struct wpx_ia2_desc *desc,
const struct wpx_section *section, void *arr_ptr,
const uint32_t nmemb, const size_t size) {
	const size_t bytes = size * nmemb;
	if (bytes == section->decomp_size) {
		// All structs for which this is called are made of u32 fields
		uint32_t **tgt = arr_ptr;
		struct wu_st st = alloc_section_data(desc, section, tgt, bytes);
		if (wu_isok(st)) {
			struct_swap(*tgt, bytes);
		}
		return st;
	}
	return wuerr(wu_invalid_header, "array decompressed size mismatch");
}

static bool list_check(struct wpx_ia2_list *list, const uint32_t nr) {
	const bool mismatch = (bool)nr ^ (bool)list->off;
	return !mismatch;
}

static struct wu_st read_list_idx(struct wpx_ia2_desc *desc,
const struct wpx_section *section, struct wpx_ia2_list *list, uint32_t nr) {
	return read_array(desc, section, &list->off, nr, sizeof(*list->off));
}

static struct wu_st read_list_str(struct wpx_ia2_desc *desc,
const struct wpx_section *section, struct wpx_ia2_list *list) {
	list->str_len = section->decomp_size;
	return alloc_section_data(desc, section, &list->str,
		section->decomp_size);
}

static struct wu_st read_frame_count(struct wpx_ia2_desc *desc,
const struct wpx_section *section) {
	struct wpx_ia2_count_t *nr = &desc->nr;
	// Reject if already seen
	if (nr->frames) {
		return wuerr(wu_invalid_header, "repeated frame count section");
	}
	const size_t bytes = sizeof(*nr);
	if (get_section_data(&desc->mp, section, nr, bytes, 1, 0) == bytes) {
		struct_swap(nr, bytes);
		return WU_OK;
	}
	return WUERR_HERE(wu_unexpected_eof);
}

struct wu_st wpx_ia2_parse(struct wpx_ia2_desc *desc, const struct wuptr mem) {
	*desc = (struct wpx_ia2_desc) {
		.mp = mp_wuptr(mem),
	};
	const uint8_t sig[] = {'W', 'P', 'X', 0x1a, 'I', 'A', '2', 0};
	enum wu_error e = fmt_sigcmp_mem(sig, sizeof(sig), &desc->mp);
	if (e != wu_ok) {
		return WUERR_HERE(e);
	}

	struct wu_st st = load_section_dir(&desc->dir, &desc->mp, 3);
	if (!wu_isok(st)) {
		return st;
	}

	if (desc->dir.sections[0].id != wpx_ia2_count) {
		return wuerr(wu_invalid_header,
			"file doesn't start with frame count");
	}
	for (uint8_t i = 0; i < desc->dir.count; ++i) {
		const struct wpx_section *s = process_section(
			desc->dir.sections + i);
		switch (s->id) {
		case wpx_ia2_count:
			st = read_frame_count(desc, s);
			break;
		case wpx_ia2_offsets:
			st = read_array(desc, s, &desc->frames, desc->nr.frames,
				sizeof(*desc->frames));
			break;
		case wpx_ia2_files:
			desc->base = s->offset;
			break;

		case wpx_ia2_geom:
			st = read_array(desc, s, &desc->geom, desc->nr.geom,
				sizeof(*desc->geom));
			break;
		case wpx_ia2_mys4:
			st = read_array(desc, s, &desc->range, desc->nr.range,
				sizeof(*desc->range));
			break;
		case wpx_ia2_mys5:
			st = read_array(desc, s, &desc->mys5, desc->nr.mys5,
				sizeof(*desc->mys5));
			break;
		case wpx_ia2_name_idx:
			st = read_list_idx(desc, s, &desc->names, desc->nr.frames);
			break;
		case wpx_ia2_names:
			st = read_list_str(desc, s, &desc->names);
			break;
		case wpx_ia2_sfx_idx:
			st = read_list_idx(desc, s, &desc->sfx, desc->nr.sfx);
			break;
		case wpx_ia2_sfx:
			st = read_list_str(desc, s, &desc->sfx);
			break;
		default:
			break;
		}
		if (!wu_isok(st)) {
			return st;
		}
	}
	if (!desc->frames || !desc->base
	|| !list_check(&desc->names, desc->nr.frames)
	|| !list_check(&desc->sfx, desc->nr.sfx)) {
		return wuerr(wu_invalid_header,
			"bad frame offsets or bad list configuration");
	}
	return WU_OK;
}

// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2024 kaleido
#include <stdlib.h>
#include <string.h>

#include "misc/bit.h"
#include "misc/common.h"
#include "misc/math.h"
#include "misc/mem.h"
#include "raster/fmt.h"
#include "pcf.h"

/* Yet another X server format, which means documentation is only helpful if
 * you're in the know. Seriously, the best resource is
https://web.archive.org/web/20060718044231/http://www.tsg.ne.jp/GANA/S/pcf2bdf/pcf.pdf
 * even if you don't know japanese.
*/

static const uint32_t COMPACT_PROP = 9;

static bool format_default(uint32_t format) {
	return (format & ~0xffu) == 0;
}
static bool format_is_compressed(uint32_t format) {
	return format & 0x100;
}
static uint8_t format_scan_unit(uint32_t format) {
	return (format >> 4) & 0x03;
}
static enum endianness format_bit_endian(uint32_t format) {
	return (format & 0x08) ? big_endian : little_endian;
}
static enum endianness format_byte_endian(uint32_t format) {
	return (format & 0x04) ? big_endian : little_endian;
}
static uint8_t format_glyph_pad(uint32_t format) {
	return format & 0x03;
}
static align_t format_align(uint32_t format) {
	return (align_t)umax(format_glyph_pad(format), format_scan_unit(format));
}

void pcf_cleanup(struct pcf_desc *desc) {
	free(desc->names.offsets);
	free(desc->prop.buf);
	free(desc->bitmap.offsets);
	free(desc->metrics);
	free(desc->toc);
}

struct pcf_cb_data {
	struct fmt_swap_info swap;
	enum endianness bit:8;
};

// TODO: find samples using u16 and u32 and get rid of all bit reversal
static void load_callback(void *restrict data, size_t len,
void *restrict user) {
	const struct pcf_cb_data *info = user;
	fmt_swap(data, len, info->swap);
	if (info->bit == little_endian) {
		len /= info->swap.depth/8;
		for (size_t i = 0; i < len; ++i) {
			switch (info->swap.depth) {
			case 16:
				;uint16_t *db = data;
				db[i] = bit_rev16(db[i]);
				break;
			case 32:
				;uint32_t *dc = data;
				dc[i] = bit_rev32(dc[i]);
				break;
			}
		}
	}
}

struct wu_st pcf_load_glyph(const struct pcf_desc *desc, struct wuimg *img,
const uint32_t i) {
	const struct pcf_bitmap *bitmap = &desc->bitmap;
	const long pos = bitmap->file_pos
		+ endian32(bitmap->offsets[i], bitmap->endian);
	fseek(desc->ifp, pos, SEEK_SET);
	const enum endianness byte = format_byte_endian(bitmap->format);
	const enum endianness bit = format_bit_endian(bitmap->format);
	const uint8_t unit = format_scan_unit(bitmap->format);
	struct pcf_cb_data data = {
		.swap = {.e = byte, .depth = (uint8_t)(8 << unit)},
		.bit = bit,
	};
	size_t r = 0;
	if (img->bit == big_endian
	&& (data.bit == little_endian || fmt_will_swap(data.swap))) {
		r = fmt_load_raster_callback(img, desc->ifp,
			load_callback, &data);
	} else {
		r = fmt_load_raster(img, desc->ifp);
	}
	return wuerr_partial(r, wuimg_size(img));
}

static bool get_pcf_str(const struct pcf_string *str, struct wuptr *out,
const uint32_t offset) {
	if (offset < str->len) {
		const uint8_t *s = str->str + offset;
		const uint32_t max = str->len - offset;
		const uint8_t *null = memchr(s, 0, max);
		if (null) {
			*out = (struct wuptr) {
				.ptr = s,
				.len = (size_t)(null - s),
			};
			return true;
		}
	}
	return false;
}

static void set_glyph_metadata(const struct pcf_names *names,
struct wuimg *img, const uint32_t i) {
	struct wuptr id;
	if (i < names->glyphs) {
		const uint32_t offset = endian32(names->offsets[i],
			names->endian);
		if (get_pcf_str(&names->str, &id, offset)) {
			struct wutree *meta = wuimg_get_metadata(img);
			if (meta) {
				tree_add_leaf_len(meta, "Name", id, NULL);
			}
		}
	}
}

struct wu_st pcf_set_glyph(struct pcf_desc *desc, struct wuimg *img,
const uint32_t i) {
	const struct pcf_metrics *metrics = desc->metrics + i;
	const int w = metrics->right_bearing - metrics->left_bearing;
	const int h = metrics->char_ascent + metrics->char_descent;
	if (w >= 0 && h >= 0) {
		const struct pcf_bitmap *bitmap = &desc->bitmap;
		img->w = (size_t)w;
		img->h = (size_t)h;
		img->channels = 1;
		img->bitdepth = 1;
		img->align_sh = format_align(bitmap->format);
		const enum endianness bit = format_bit_endian(bitmap->format);
		img->bit = format_scan_unit(bitmap->format) == 0
			? (bit & 1) : big_endian;
		set_glyph_metadata(&desc->names, img, i);
		return WU_OK;
	}
	return wuerr(wu_invalid_header, "negative bitmap dimensions");
}

struct wu_st pcf_get_property(const struct pcf_desc *desc,
struct pcf_property *out, const uint32_t i) {
	const struct pcf_prop *prop = &desc->prop;
	const uint8_t *buf = prop->buf + COMPACT_PROP * i;
	const uint32_t name_offset = buf_endian32(buf, desc->prop.endian);
	const uint32_t value = buf_endian32(buf + 5, desc->prop.endian);

	out->is_string = buf[4];
	if (!get_pcf_str(&prop->str, &out->name, name_offset)) {
		return wuerr(wu_invalid_header, "property name out of bounds");
	}
	if (out->is_string) {
		if (!get_pcf_str(&prop->str, &out->val.s, value)) {
			return wuerr(wu_invalid_header,
				"property string value out of bounds");
		}
	} else {
		out->val.i = value;
	}
	return WU_OK;
}

static struct wu_st parse_bitmap(struct pcf_desc *desc, struct pcf_toc *t,
const enum endianness e) {
	uint32_t glyphs;
	if (!format_default(t->format) || format_align(t->format) > 2) {
		return wuerr(wu_invalid_header, "bad format or alignment");
	} else if (!fread(&glyphs, sizeof(glyphs), 1, desc->ifp)) {
		return WUERR_HERE(wu_unexpected_eof);
	}

	struct pcf_bitmap *bitmap = &desc->bitmap;
	glyphs = endian32(glyphs, e);
	bitmap->endian = e;
	/* This can overflow on a 32-bit system. It's unlikely anyone expected
	 * such giant files, but since it just results in reading the bitmap
	 * from the wrong offset, we don't care. */
	bitmap->file_pos = (long)(t->offset
		+ (2 + glyphs + 4) * sizeof(*bitmap->offsets));
	desc->glyphs = u32min(desc->glyphs, glyphs);

	bitmap->format = t->format;
	bitmap->offsets = small_malloc(desc->glyphs, sizeof(*bitmap->offsets));
	if (!bitmap->offsets) {
		return WUERR_HERE(wu_alloc_error);
	}

	if (fread(bitmap->offsets, sizeof(*bitmap->offsets), desc->glyphs,
	desc->ifp) != desc->glyphs) {
		return WUERR_HERE(wu_unexpected_eof);
	}
	return WU_OK;
}

static int16_t to_uncomp(uint8_t n) {
	return (int16_t)n - 0x80;
}

static struct wu_st parse_metrics(struct pcf_desc *desc, struct pcf_toc *t,
const enum endianness e) {
	struct compressed_metrics {
		uint8_t left_bearing;
		uint8_t right_bearing;
		uint8_t char_width;
		uint8_t char_ascent;
		uint8_t char_descent;
	};
	struct compressed_metrics *comp;

	const bool compressed = format_is_compressed(t->format);
	uint8_t head[4];
	const size_t read = fread(head,
		compressed ? sizeof(uint16_t) : sizeof(uint32_t), 1, desc->ifp);
	desc->glyphs = u32min(desc->glyphs,
		compressed ? buf_endian16(head, e) : buf_endian32(head, e));
	if (!read) {
		return WUERR_HERE(wu_unexpected_eof);
	}

	struct pcf_metrics *metrics = small_malloc(desc->glyphs, sizeof(*metrics));
	if (!metrics) {
		return WUERR_HERE(wu_alloc_error);
	}
	desc->metrics = metrics;

	const size_t struct_size = compressed ? sizeof(*comp) : sizeof(*metrics);
	void *buf = (uint8_t *)metrics
		+ desc->glyphs * (sizeof(*metrics) - struct_size);
	desc->glyphs = (uint32_t)fread(buf, struct_size, desc->glyphs, desc->ifp);
	if (compressed) {
		comp = buf;
		for (size_t i = 0; i < desc->glyphs; ++i) {
			struct compressed_metrics *m = comp + i;
			metrics[i] = (struct pcf_metrics) {
				.left_bearing = to_uncomp(m->left_bearing),
				.right_bearing = to_uncomp(m->right_bearing),
				.char_width = to_uncomp(m->char_width),
				.char_ascent = to_uncomp(m->char_ascent),
				.char_descent = to_uncomp(m->char_descent),
			};
		}
	} else {
		endian_loop16(buf, e, desc->glyphs);
	}
	return WU_OK;
}
static struct wu_st load_string_struct(struct pcf_toc *t,
const enum endianness e, FILE *ifp, uint32_t *count, enum endianness *out,
uint8_t **buf, struct pcf_string *str, const size_t elem_size) {
	const uint32_t head_size = sizeof(t->format) + sizeof(*count);
	if (!format_default(t->format) || t->size < head_size) {
		return wuerr(wu_invalid_header,
			"bad struct format or too low size");
	} else if (!fread(count, sizeof(*count), 1, ifp)) {
		return WUERR_HERE(wu_unexpected_eof);
	}
	*count = endian32(*count, e);
	if (UINT32_MAX / elem_size - sizeof(str->len) > *count) {
		const size_t middle_size = *count * elem_size + sizeof(str->len);
		const uint32_t rem = t->size - head_size;
		*out = e;
		*buf = small_malloc(rem, 1);
		if (!*buf) {
			return WUERR_HERE(wu_alloc_error);
		}
		const size_t read = fread(*buf, 1, rem, ifp);
		if (read < middle_size) {
			return WUERR_HERE(wu_unexpected_eof);
		}
		*str = (struct pcf_string) {
			/* StringLen field has the wrong endianness sometimes,
			 * so deduct from struct size as it's more reliable. */
			.len = (uint32_t)(read - middle_size),
			.str = *buf + middle_size,
		};
		return WU_OK;
	}
	return wuerr(wu_int_overflow, "u32 overflow on string struct size");
}

static struct wu_st parse_glyph_names(struct pcf_desc *desc, struct pcf_toc *t,
const enum endianness e) {
	/* Glyph names struct (after format):
		Offset  Type    Name
		4       i32     GlyphCount
		8       i32     Offsets[GlyphCount]
		+0      i32     StringLen
		+4      char    String[StringLen]
	*/
	struct pcf_names *names = &desc->names;
	uint8_t *buf = NULL;
	const struct wu_st st = load_string_struct(t, e, desc->ifp,
		&names->glyphs, &names->endian, &buf, &names->str,
		sizeof(*names->offsets));
	names->offsets = (uint32_t *)buf;
	return st;
}


static struct wu_st parse_properties(struct pcf_desc *desc, struct pcf_toc *t,
const enum endianness e) {
	/* Properties struct (after format):
		Offset  Type    Name
		4       i32     NProps
		8       struct  Props[NProps]
		...
		var     u8      Padding       // To next 32-bit word.
		+0      i32     StringLen
		+4      char    Strings[StringLen]
		...
		var     u8      Padding

	 * Props struct:
		Offset  Type    Name
		0       i32     Name     // Offset within Strings
		4       u8      IsString // If 0, Value is an integer
		5       i32     Value    //  if 1, value is a Strings offset
		9
	*/
	struct pcf_prop *prop = &desc->prop;
	uint8_t *buf = NULL;
	const struct wu_st st = load_string_struct(t, e, desc->ifp,
		&prop->len, &prop->endian, &buf, &prop->str, COMPACT_PROP);
	prop->buf = buf;
	return st;
}

struct wu_st pcf_parse(struct pcf_desc *desc, FILE *ifp) {
	/* PCF header (after magic bytes):
		Offset  Type    Name
		0       u8      Magic[4]
		4       u32_le  TableCount
		8       struct  TOC[TableCount]

	 * TOC describes the structures present in the file. Structures can
	 * only appear once.
	 * TOC struct:
		Offset  Type    Name
		0       u32_le  StructType
		4       u32_le  Format     // How the struct is formatted
		8       u32_le  Size
		12      u32_le  Offset     // Absolute offset in file
		16

	 * All structs begin with:
		Offset  Type    Name
		0       u32_le  Format // Same as Format in TOC

	 * Format is a bitfield:
		0 - 2 3 4 - 6 - 8 9
		| | | | | | | | | +-- Inkbounds. Unused???
		|/  | | |/  |/  +---- AccelWithInkbounds or CompressedMetrics
		|   | | |   |          structure variants.
		|   | | |   +-------- Unused.
		|   | | +------------ ScanUnit. Bitmap is stored using
		|   | |                (8 << x)-bit words
		|   | +-------------- MSBitFirst. If 1, pixels are in MS-to-LS
		|   |                  order within each Unit. If 0, LS-to-MS
		|   +---------------- MSByteFirst. If 1, struct fields are big
		|                      endian.
		+-------------------- RowPad. Bitmap rows are padded to
		                       (1 << x)-byte boundaries.
	*/
	*desc = (struct pcf_desc) {.ifp = ifp};
	const unsigned char magic[] = {0x01, 'f', 'c', 'p'};
	uint32_t buf[2];
	if (!fread(buf, sizeof(buf), 1, desc->ifp)) {
		return WUERR_HERE(wu_unexpected_eof);
	} else if (memcmp(buf, magic, sizeof(magic))) {
		return WUERR_HERE(wu_invalid_signature);
	}
	desc->toc_len = endian32l(buf[1]);

	struct pcf_toc *toc;
	const size_t toc_size = desc->toc_len * sizeof(*toc);
	toc = small_malloc(desc->toc_len, sizeof(*toc));
	if (!toc) {
		return WUERR_HERE(wu_alloc_error);
	}
	desc->toc = toc;
	if (!fread(toc, toc_size, 1, desc->ifp)) {
		return WUERR_HERE(wu_unexpected_eof);
	}

	/* Glyphs is repeated a lot of times throughout the file. Rather than
	 * check every instance, just call u32min() on each read. It's on the
	 * encoder to keep things consistent. */
	desc->glyphs = ~(uint32_t)0;
	for (uint32_t i = 0; i < desc->toc_len; ++i) {
		struct pcf_toc *t = toc + i;
		endian_loop32((uint32_t *)t, little_endian,
			sizeof(*t) / sizeof(t->type));
		if ((desc->seen & t->type)) {
			return wuerr(wu_invalid_header, "repeated struct type");
		} else if ((t->format & ~0x1ffu)) {
			return wuerr(wu_invalid_header, "bad struct format");
		}
		desc->seen |= (uint16_t)t->type;

		fseek(desc->ifp, t->offset, SEEK_SET);
		uint32_t format;
		if (!fread(&format, sizeof(format), 1, desc->ifp)) {
			return WUERR_HERE(wu_unexpected_eof);
		} else if (format != t->format) {
			return wuerr(wu_invalid_header, "struct format mismatch");
		}

		struct wu_st st;
		const enum endianness e = format_byte_endian(format);
		switch (t->type) {
		case pcf_type_metrics:
			st = parse_metrics(desc, t, e);
			break;
		case pcf_type_bitmaps:
			st = parse_bitmap(desc, t, e);
			break;
		case pcf_type_properties:
			st = parse_properties(desc, t, e);
			break;
		case pcf_type_glyph_names:
			st = parse_glyph_names(desc, t, e);
			break;
		case pcf_type_accelerators:
		case pcf_type_ink_metrics:
		case pcf_type_bdf_encodings:
		case pcf_type_swidths:
		case pcf_type_bdf_accelerators:
			continue;
		default:
			return wuerr(wu_invalid_header, "unknown struct type");
		}
		if (!wu_isok(st)) {
			return st;
		}
	}

	// Not sure if glyph_names are mandatory, but all valid files have them
	const uint16_t required = pcf_type_metrics
		| pcf_type_bitmaps
		| pcf_type_glyph_names;
	if ((desc->seen & required) == required) {
		return desc->glyphs ? WU_OK : WUERR_HERE(wu_no_image_data);
	}
	return wuerr(wu_invalid_header, "file lacks required structures");
}

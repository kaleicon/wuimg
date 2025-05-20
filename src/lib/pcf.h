// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2024 kaleido
#ifndef LIB_PCF
#define LIB_PCF

#include "misc/endian.h"
#include "raster/wuimg.h"

enum pcf_type {
	pcf_type_properties = 1 << 0,
	pcf_type_accelerators = 1 << 1,
	pcf_type_metrics = 1 << 2,
	pcf_type_bitmaps = 1 << 3,
	pcf_type_ink_metrics = 1 << 4,
	pcf_type_bdf_encodings = 1 << 5,
	pcf_type_swidths = 1 << 6,
	pcf_type_glyph_names = 1 << 7,
	pcf_type_bdf_accelerators = 1 << 8,
};

struct pcf_string {
	uint32_t len;
	uint8_t *str;
};

struct pcf_names {
	enum endianness endian;
	uint32_t glyphs;
	uint32_t *offsets;
	struct pcf_string str;
};

struct pcf_bitmap {
	uint32_t format;
	uint32_t *offsets;
	long file_pos;
};

struct pcf_metrics {
	int16_t left_bearing;
	int16_t right_bearing;
	int16_t char_width;
	int16_t char_ascent;
	int16_t char_descent;
	uint16_t attr;
};

struct pcf_property {
	struct wuptr name;
	bool is_string;
	union {
		struct wuptr s;
		uint32_t i;
	} val;
};

struct pcf_prop {
	enum endianness endian;
	uint32_t len;
	uint8_t *buf;
	struct pcf_string str;
};

struct pcf_toc {
	uint32_t type;
	uint32_t format;
	uint32_t size;
	uint32_t offset;
};

struct pcf_desc {
	FILE *ifp;
	uint32_t toc_len;
	uint32_t glyphs;
	struct pcf_toc *toc;
	struct pcf_metrics *metrics;
	struct pcf_prop prop;
	struct pcf_bitmap bitmap;
	struct pcf_names names;
	uint32_t cur_offset;
	uint16_t seen;
};

void pcf_cleanup(struct pcf_desc *desc);

size_t pcf_load_glyph(const struct pcf_desc *desc, struct wuimg *img);

enum wu_error pcf_set_glyph(struct pcf_desc *desc, struct wuimg *img,
uint32_t i);

enum wu_error pcf_get_property(const struct pcf_desc *desc, uint32_t i,
struct pcf_property *out);

enum wu_error pcf_parse(struct pcf_desc *desc);

enum wu_error pcf_open(struct pcf_desc *desc, FILE *ifp);

#endif /* LIB_PCF */

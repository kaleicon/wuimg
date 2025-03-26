// SPDX-License-Identifier: 0BSD
#ifndef WUAUTO
#define WUAUTO

#include "wudefs.h"
#include "misc/endian.h"

enum auto_dst {
	auto_match = 0,
	auto_skip,
	auto_fn,
	auto_width = 'w',
	auto_height = 'h',
};

typedef bool (*auto_fn_t)(struct wuimg *img, const uint8_t *restrict src, uint8_t len);

struct auto_read {
	enum auto_dst dst:8;
	uint8_t size;
	union {
		const uint8_t *bytes;
		auto_fn_t fn;
	} u;
};

struct auto_desc {
	uint16_t w, h;
	uint8_t channels;
	uint8_t bitdepth;
	uint8_t used_bits;
	enum pix_layout layout:8;
	enum pix_attr attr:8;
	enum alpha_interpretation alpha:2;
	uint16_t bitfield;
	enum endianness endian:8;
	uint8_t rlen;
	const struct auto_read *read;
};

enum wu_error auto_load(struct image_file *infile, const struct auto_desc *desc);

enum wu_error auto_init(struct image_file *infile, const struct wu_conf *conf,
const struct auto_desc *desc);

#endif /* WUAUTO */

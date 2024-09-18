#ifndef WUAUTO
#define WUAUTO

#include "wudefs.h"
#include "misc/endian.h"

enum auto_dst {
	auto_match = 0,
	auto_width = 'w',
	auto_height = 'h',
};

struct auto_read {
	enum auto_dst dst:8;
	uint8_t size;
	const uint8_t *bytes;
};

struct auto_desc {
	uint16_t w, h;
	uint8_t channels;
	uint8_t bitdepth;
	enum pix_layout layout:8;
	enum endianness endian:8;
	uint8_t rlen;
	const struct auto_read *read;
};

enum wu_error auto_load(struct image_file *infile, const struct auto_desc *desc);

enum wu_error auto_init(struct image_file *infile, const struct wu_conf *conf,
const struct auto_desc *desc);

#endif /* WUAUTO */

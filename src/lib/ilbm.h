// SPDX-License-Identifier: 0BSD
#ifndef LIB_ILBM
#define LIB_ILBM

#include "misc/endian.h"
#include "raster/wuimg.h"

struct ilbm_tiny {
	bool present;
	uint16_t w, h;
	struct wuptr data;
};

enum ilbm_masking {
	ilbm_masking_none = 0,
	ilbm_masking_bitplane = 1,
	ilbm_masking_value = 2,
	ilbm_masking_lasso = 3,
};

enum ilbm_compression {
	ilbm_compression_none = 0,
	ilbm_compression_packbits = 1,
};

enum ilbm_format {
	ilbm_format_ilbm = FOURCC('I', 'L', 'B', 'M'),
	ilbm_format_pbm = FOURCC('P', 'B', 'M', ' '),
};

typedef enum wu_error (*ilbm_callback_t)(uint32_t type, struct wuptr data,
	void *ptr);

struct ilbm_desc {
	ilbm_callback_t callback;
	ilbm_callback_t text_callback;
	void *restrict usr_ptr;

	struct mparser mp;
	struct wuptr body;
	struct palette *pal;
	struct palette_cycle *cycle;
	struct ilbm_tiny tiny;

	enum ilbm_format format;
	unsigned colors;
	uint8_t planes;
	enum ilbm_masking masking:8;
	enum ilbm_compression compression:8;
	uint8_t trans_value;
	bool extra_half_brite;
	bool ham;
};

void ilbm_cleanup(struct ilbm_desc *desc);

size_t ilbm_decode_tiny(const struct ilbm_desc *desc, struct wuimg *main,
struct wuimg *tiny);

size_t ilbm_decode_main(const struct ilbm_desc *desc, struct wuimg *img);

enum wu_error ilbm_parse_footer(struct ilbm_desc *desc);

enum wu_error ilbm_parse_header(struct ilbm_desc *desc, struct wuimg *img);

void ilbm_set_callbacks(struct ilbm_desc *desc, ilbm_callback_t callback,
ilbm_callback_t text_callback, void *restrict usr_ptr);

enum wu_error ilbm_open(struct ilbm_desc *desc, const struct mparser mp);

#endif /* LIB_ILBM */

// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2025 kaleido
#ifndef LIB_ELECBYTE
#define LIB_ELECBYTE

#include "misc/mparser.h"
#include "raster/wuimg.h"

size_t eb_print_version(char str[static 16], const uint8_t version[static 4]);


enum eb_sff_decoder {
	eb_sff_pcx,
	eb_sff_sff,
	eb_sff_png,
};

enum eb_sff2_format {
	eb_sff2_raw = 0,
	//eb_sff2_invalid = 1,
	eb_sff2_rle8 = 2,
	eb_sff2_rle5 = 3,
	eb_sff2_lz5 = 4,
	eb_sff2_png8 = 10,
	eb_sff2_png24 = 11,
	eb_sff2_png32 = 12,
};

struct eb_sff2_sub {
	uint16_t group, item;
	uint16_t w, h;
	uint16_t x, y;
	enum eb_sff2_format fmt:8;
	uint8_t depth;
	struct palette *pal;
};

struct eb_sff1_sub {
	int16_t x, y;
	int16_t group, image;
	int16_t prev;
	bool shared_pal;
	struct wuptr comm;
};

struct eb_sff_sub {
	struct wuptr data;
	union eb_sff_sub_v {
		struct eb_sff1_sub v1;
		struct eb_sff2_sub v2;
	} u;
};

struct eb_sff2 {
	uint32_t cur;
	uint32_t image_off;
	uint32_t pal_off;
	uint32_t pal_nr;
	struct palette **pals;
	struct wuptr ldata, tdata;
};

struct eb_sff1 {
	uint32_t groups;
	uint32_t first;
	bool shared_pal;
};

struct eb_sff_desc {
	struct mparser mp;
	uint8_t version[4];
	uint32_t images;
	struct wuptr comm;
	union eb_sff_v {
		struct eb_sff1 v1;
		struct eb_sff2 v2;
	} u;
};

const char * eb_sff2_format_str(enum eb_sff2_format fmt);

uint8_t eb_sff_get_version(const struct eb_sff_desc *desc);

enum eb_sff_decoder eb_sff_get_decoder(const struct eb_sff_desc *desc,
const struct eb_sff_sub *sub);


void eb_sff_cleanup(struct eb_sff_desc *desc);

void eb_sff_touchup(const struct eb_sff_desc *desc,
const struct eb_sff_sub *sub, struct wuimg *dst, struct wuimg *first);

struct wu_st eb_sff2_dec(const struct eb_sff_sub *sub, struct wuimg *img);

struct wu_st eb_sff2_get_dims(const struct eb_sff_sub *sub, struct wuimg *img);

struct wu_st eb_sff_next(struct eb_sff_desc *desc, struct eb_sff_sub *sub);

struct wu_st eb_sff_parse(struct eb_sff_desc *desc, struct wuptr mem);


struct eb_fnt_desc {
	struct wuptr pcx, text, comment;
	uint8_t version[4];
};

struct wu_st eb_fnt_parse(struct eb_fnt_desc *desc, struct wuptr mem);

#endif /* LIB_ELECBYTE */

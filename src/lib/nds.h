// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2026 kaleido
#ifndef LIB_NDS
#define LIB_NDS

#include "misc/mparser.h"
#include "raster/wuimg.h"

struct g2d_desc {
	struct mparser mp;
	uint8_t version_major, version_minor;
	uint16_t nr_blocks;
};

enum nds_texfmt {
	nds_texfmt_pltt16 = 3,
	nds_texfmt_pltt256 = 4,
};

enum nds_scrfmt {
	nds_scrfmt_text = 0,
	nds_scrfmt_affine = 1,
	nds_scrfmt_affine_ext = 2,
	nds_scrfmt_pltbmp = 3,
	nds_scrfmt_dcbmp = 4,
};

enum nds_mapping {
	nds_mapping_2d = 0,
	nds_mapping_1d_32k = 1 << 4 | 0 << 20,
	nds_mapping_1d_64k = 1 << 4 | 1 << 20,
	nds_mapping_1d_128k = 1 << 4 | 2 << 20,
	nds_mapping_1d_256k = 1 << 4 | 3 << 20,
};

enum nds_charfmt {
	nds_charfmt_char = 0,
	nds_charfmt_bmp = 1,
};

enum nds_colormode {
	nds_colormode_16x16 = 0,
	nds_colormode_256x1 = 1,
	nds_colormode_256x16 = 2,
};

const char * nds_texfmt_str(enum nds_texfmt fmt);
const char * nds_scrfmt_str(enum nds_scrfmt fmt);
const char * nds_mapping_str(enum nds_mapping mapping);
const char * nds_charfmt_str(enum nds_charfmt fmt);
const char * nds_colormode_str(enum nds_colormode color);

struct wu_st nds_pal_as_img_info(struct wuimg *img);


struct nclr_desc {
	struct g2d_desc g2d;
	struct wuptr data;
	enum nds_texfmt fmt;
	uint32_t pal_size;
};

struct wu_st nclr_into_img(const struct nclr_desc *desc, struct wuimg *img);

struct wu_st nclr_img_info(struct wuimg *img);

struct wu_st nclr_init(struct nclr_desc *desc, struct wuptr mem);


struct ncgr_desc {
	struct g2d_desc g2d;
	struct nclr_desc nclr;
	uint16_t h, w;
	uint32_t graphics_size;
	struct wuptr data;

	enum nds_texfmt fmt;
	enum nds_mapping mapping;
	enum nds_charfmt charfmt:8;
	bool mapping_1d;
	struct palette *pal;
};

void ncgr_cleanup(struct ncgr_desc *desc);

struct wu_st ncgr_load(const struct ncgr_desc *desc, struct wuimg *img);

struct wu_st ncgr_img_info(struct ncgr_desc *desc, struct wuimg *img);

struct wu_st ncgr_search_nclr(struct ncgr_desc *desc, const char *ncgr_name);

struct wu_st ncgr_init(struct ncgr_desc *desc, struct wuptr mem);


struct nscr_desc {
	struct g2d_desc g2d;
	struct ncgr_desc ncgr;

	enum nds_colormode color;
	enum nds_scrfmt fmt;
	bool substract;
	struct wuptr data;
};

void nscr_cleanup(struct nscr_desc *desc);

struct wu_st nscr_decode(const struct nscr_desc *desc, struct wuimg *img);

struct wu_st nscr_init(struct nscr_desc *desc, struct wuimg *img,
struct wuptr mem, const char *name);


struct wu_st ancl_into_img(struct wuimg *img, FILE *ifp);


struct wu_st atex_parse(struct wuimg *img, FILE *ifp, const char *filename);


struct bgd_desc {
	uint16_t nr_tiles;
	uint16_t pal_entries;
	const uint8_t *data;
	const uint8_t *idx;
	const uint8_t *pal;
};

struct wu_st bgd_decode(const struct bgd_desc *desc, struct wuimg *img);

struct wu_st bgd_init(struct bgd_desc *desc, struct wuimg *img,
const struct wuptr mem);


struct wu_st r00_parse_next(struct wuimg *img, FILE *ifp);

#endif /* LIB_NDS */

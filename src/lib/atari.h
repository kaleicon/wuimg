// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2024 kaleido
#ifndef LIB_DEGAS
#define LIB_DEGAS

#include "misc/wustr.h"
#include "raster/wuimg.h"

enum atari_st_res {
	atari_st_res_low = 0,
	atari_st_res_medium = 1,
	atari_st_res_high = 2,
};

const char * atari_st_res_str(enum atari_st_res res);

/* Dali */
struct dali_desc {
	FILE *ifp;
	enum atari_st_res res;
};

size_t dali_decode(struct dali_desc *desc, struct wuimg *img);

enum wu_error dali_parse(struct dali_desc *desc, struct wuimg *img, FILE *ifp,
const uint8_t ext[static 3]);

/* DEGAS */
struct degas_desc {
	FILE *ifp;
	size_t size;
	enum atari_st_res res:8;
	bool compressed;
	bool is_elite;
	struct palette_cycle *cycle;
};

void degas_cleanup(struct degas_desc *desc);

size_t degas_decode(struct degas_desc *desc, struct wuimg *img);

enum wu_error degas_parse(struct degas_desc *desc, struct wuimg *img,
FILE *ifp);

/* EZ-Art Professional */
size_t ez_decode(struct mparser mp, struct wuimg *img);

enum wu_error ez_parse(struct mparser *mp, struct wuimg *img, struct wuptr mem);

/* GFA Raytrace */
struct gfa_desc {
	FILE *ifp;
	bool compressed;
	enum atari_st_res res:8;
	uint32_t factor;
	uint32_t frames;
	struct gfa_frame {
		uint32_t off, len;
	} frame[10];
};

struct wu_st gfa_decode(const struct gfa_desc *desc, struct wuimg *img,
uint8_t frame);

struct wu_st gfa_init(struct gfa_desc *desc, struct wuimg *img, FILE *ifp);

/* MegaPaint */
struct bld_desc {
	FILE *ifp;
	bool compressed;
};

size_t bld_decode(struct bld_desc *desc, struct wuimg *img);

enum wu_error bld_parse(struct bld_desc *desc, struct wuimg *img, FILE *ifp);

/* Spectrum 512 */
struct spu_desc {
	FILE *ifp;
	bool enhanced;
};

struct wu_st spu_decode(const struct spu_desc *desc, struct wuimg *img);

struct wu_st spu_init(struct spu_desc *desc, struct wuimg *img, FILE *ifp);

/* STAD PAC, Arabesque */
struct stad_desc {
	struct mparser mp;
	uint8_t sig[6];
	uint16_t block_nr;
	uint16_t block[4];
};

struct wu_st stad_decode(const struct stad_desc *desc, struct wuimg *img);

struct wu_st stad_init(struct stad_desc *desc, struct wuimg *img,
struct wuptr mem);

/* Tiny Stuff */
struct tiny_desc {
	struct mparser mp;
	enum atari_st_res res:8;
	uint16_t ctrl;
	uint16_t data;
	uint16_t iters;
	struct palette_cycle *cycle;
};

void tiny_cleanup(struct tiny_desc *desc);

size_t tiny_decode(const struct tiny_desc *desc, struct wuimg *img);

enum wu_error tiny_parse(struct tiny_desc *desc, struct wuimg *img,
struct wuptr mem);

#endif /* LIB_DEGAS */

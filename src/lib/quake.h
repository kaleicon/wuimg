// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2024 kaleido
#ifndef LIB_IDSP
#define LIB_IDSP

#include "raster/wuimg.h"

enum idsp_version {
	idsp_quake = 1,
	idsp_half_life = 2,
	idsp_rgba = 32,
};

enum idsp_type {
	idsp_vp_parallel_upright = 0,
	idsp_facing_upright = 1,
	idsp_vp_parallel = 2,
	idsp_oriented = 3,
	idsp_vp_parallel_oriented = 4,
};

enum idsp_alpha {
	idsp_alpha_normal = 0,
	idsp_alpha_additive = 1,
	idsp_alpha_indexed = 2,
	idsp_alpha_test = 3,
};

enum idsp_synch {
	idsp_synchronized = 0,
	idsp_random = 1,
};

struct idsp_desc {
	FILE *ifp;

	enum idsp_version version:8;
	enum idsp_type type:8;
	enum idsp_alpha alpha:8;
	enum idsp_synch synch:8;
	float radius;
	uint32_t w, h;
	uint32_t frames;
	float beam_length;
	uint16_t entries;
	struct palette *pal;
};

const char * idsp_type_str(enum idsp_type t);

const char * idsp_alpha_str(enum idsp_alpha a);

const char * idsp_synch_str(enum idsp_synch s);

void idsp_cleanup(struct idsp_desc *desc);

size_t idsp_read_image(const struct idsp_desc *desc, struct wuimg *img);

enum wu_error idsp_next_image(struct idsp_desc *desc, struct wuimg *img);

enum wu_error idsp_init(struct idsp_desc *desc, FILE *ifp);


enum wu_error lmp_init(struct wuimg *img, FILE *ifp);

#endif /* LIB_IDSP */

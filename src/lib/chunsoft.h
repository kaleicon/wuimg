// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2025 kaleido
#ifndef LIB_CHUNSOFT
#define LIB_CHUNSOFT

#include "raster/wuimg.h"
#include "misc/mparser.h"

struct sir0_base {
	struct mparser mp;
	uint32_t header_off, pointer_off;
};

struct sir0_spr_desc {
	struct mparser mp;
	struct palette *pal;
	struct compost fr;
	uint32_t raster_off, sprite_off;
	const uint8_t *anim_ptrs;
	uint32_t nb_tiles;
	uint8_t nb_anim;
	uint8_t nb_images;
};

void sir0_spr_cleanup(struct sir0_spr_desc *desc);

struct wu_st sir0_spr_read_tile(struct sir0_spr_desc *desc, struct wuimg *img);

struct wu_st sir0_spr_set_tile(struct sir0_spr_desc *desc, struct wuimg *img,
uint32_t i);

struct wu_st sir0_spr_assemble_frame(struct sir0_spr_desc *desc,
struct wuimg *img, uint8_t i, uint16_t frame);

struct wu_st sir0_spr_assemble(struct sir0_spr_desc *desc, struct wuimg *img);

struct wu_st sir0_spr_assemble_info(struct sir0_spr_desc *desc,
struct wuimg *img, uint8_t i);

struct wu_st sir0_spr_init(struct sir0_spr_desc *desc, const struct wuptr mem);


struct at6p_desc {
	struct mparser mp;
	uint8_t *decomp;
};

void at6p_cleanup(struct at6p_desc *desc);

struct wu_st at6p_load(struct at6p_desc *desc, struct wuimg *img);

struct wu_st at6p_info(struct at6p_desc *desc, struct wuimg *img);

struct wu_st at6p_unpack(struct at6p_desc *desc, struct wuptr map);

#endif /* LIB_CHUNSOFT */

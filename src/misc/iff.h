// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2025 kaleido
#ifndef MISC_IFF
#define MISC_IFF
#include <stdint.h>

#include "misc/endian.h"
#include "misc/mparser.h"
#include "raster/err.h"
#include "raster/strip.h"

struct iff_chunk {
	uint32_t id;
	uint32_t len;
};

struct iff_state;

typedef struct wu_st (*iff_fn_t)(struct iff_state *iff, void *ptr,
	struct iff_chunk);

struct iff_table {
	uint32_t id;
	iff_fn_t fn;
};

struct iff_state {
	const struct iff_table *table;
	unsigned table_len;
	enum endianness endian:8;
	enum endianness id_endian:8;
	align_t align_sh;
	iff_fn_t fallback;
	void *user;
};

uint32_t iff_chunk_padding(const struct iff_state *iff, struct iff_chunk chunk);

bool iff_is_text(uint32_t id);

struct wu_st iff_search(struct iff_state *iff, void *_p, struct iff_chunk chunk);

struct wu_st iff_swap(struct iff_state *iff, void *_p, struct iff_chunk chunk);

struct wu_st iff_next_FILE(struct iff_state *iff, FILE *fp,
struct iff_chunk prev_chunk);

struct wu_st iff_skip_FILE(struct iff_state *iff, FILE *fp,
struct iff_chunk prev_chunk);

struct wu_st iff_next_mparser(struct iff_state *iff, struct mparser *mp,
struct iff_chunk prev_chunk);

#endif /* MISC_IFF */

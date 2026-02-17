// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2025 kaleido
#include <stdio.h>
#include <string.h>

#include "misc/common.h"
#include "misc/iff.h"

uint32_t iff_chunk_padding(const struct iff_state *iff,
const struct iff_chunk chunk) {
	const uint32_t a = ~(uint32_t)0 << iff->align_sh;
	return ((chunk.len + ~a) & a) - chunk.len;
}

bool iff_is_text(const uint32_t id) {
	const uint32_t text[] = {
		FOURCC('(', 'C', ')', ' '),
		FOURCC('A', 'N', 'N', 'O'),
		FOURCC('A', 'U', 'T', 'H'),
		FOURCC('D', 'O', 'C', ' '),
		FOURCC('F', 'O', 'O', 'T'),
		FOURCC('H', 'E', 'A', 'D'),
		FOURCC('P', 'A', 'R', 'A'),
		FOURCC('P', 'D', 'E', 'F'),
		FOURCC('T', 'A', 'B', 'S'),
		FOURCC('T', 'E', 'X', 'T'),
		FOURCC('V', 'E', 'R', 'S'),
	};
	for (size_t i = 0; i < ARRAY_LEN(text); ++i) {
		if (id == text[i]) {
			return true;
		}
	}
	return false;
}

static iff_fn_t linear_search(const uint32_t id,
const struct iff_table *table, const size_t len, const iff_fn_t fallback) {
	for (size_t i = 0; i < len; ++i) {
		if (id == table[i].id) {
			return table[i].fn;
		}
	}
	return fallback;
}

struct wu_st iff_search(struct iff_state *iff, void *ptr,
struct iff_chunk chunk) {
	ptr = iff->user;
	const iff_fn_t fn = linear_search(chunk.id, iff->table, iff->table_len,
		iff->fallback);
	return fn
		? fn(iff, ptr, chunk)
		: wuerr(wu_invalid_header, "unexpected chunk");
}

struct wu_st iff_swap(struct iff_state *iff, void *_p, struct iff_chunk chunk) {
	chunk.id = endian32(chunk.id, iff->id_endian);
	chunk.len = endian32(chunk.len, iff->endian);
	return iff_search(iff, _p, chunk);
}

struct wu_st iff_next_FILE(struct iff_state *iff, FILE *fp,
struct iff_chunk prev_chunk) {
	fseek(fp, iff_chunk_padding(iff, prev_chunk), SEEK_CUR);
	struct iff_chunk next;
	return fread(&next, sizeof(next), 1, fp)
		? iff_swap(iff, fp, next)
		: WUERR_HERE(wu_unexpected_eof);
}

struct wu_st iff_skip_FILE(struct iff_state *iff, FILE *fp,
struct iff_chunk prev_chunk) {
	fseek(fp, prev_chunk.len, SEEK_CUR);
	return iff_next_FILE(iff, fp, prev_chunk);
}

struct wu_st iff_next_mparser(struct iff_state *iff, struct mparser *mp,
struct iff_chunk prev_chunk) {
	mp_seek_cur(mp, iff_chunk_padding(iff, prev_chunk));
	const uint8_t *c = mp_slice(mp, sizeof(prev_chunk));
	if (c) {
		struct iff_chunk next;
		memcpy(&next, c, sizeof(next));
		return iff_swap(iff, mp, next);
	}
	return WUERR_HERE(wu_unexpected_eof);
}

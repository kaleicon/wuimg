// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2025 kaleido
#ifndef WUENC
#define WUENC

#include "raster/wuimg.h"

typedef bool (*enc_best_fit_t)(struct wuimg *dst, const struct wuimg *src);
typedef const char * (*enc_init_t)(void *state, const struct wuimg *dst,
	const struct wuimg *src, FILE *ofp);
typedef size_t (*enc_write_row_t)(void *state, const struct wuimg *dst,
	FILE *ofp, uint8_t *restrict row);
typedef size_t (*enc_write_frame_t)(void *state, const struct wuimg *dst,
	FILE *ofp, const int frame);
typedef void (*enc_end_t)(void *state);

struct enc_fn {
	uint16_t state_size;
	bool anim;
	enc_best_fit_t best_fit;
	enc_init_t init;
	enc_write_row_t write_row;
	enc_write_frame_t write_frame;
	enc_end_t end;
};

#endif // WUENC

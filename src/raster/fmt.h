// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2022 kaleido
#ifndef COMMON_FMT
#define COMMON_FMT

#include <stdio.h>

#include "misc/endian.h"
#include "misc/mparser.h"
#include "raster/pal.h"
#include "raster/wuimg.h"

enum fmt_pal_type {
	fmt_pal_rgb = 3,
	fmt_pal_rgbx = 4,
};

struct fmt_swap_info {
	enum endianness e:8;
	uint8_t depth;
};

bool fmt_will_swap(const struct fmt_swap_info info);

void fmt_swap(void *restrict data, size_t len, struct fmt_swap_info info);

/* Function called on each row as they get read. */
typedef void (*fmt_load_callback_t)(void *restrict data, size_t len,
	void *restrict user);

size_t fmt_load_raster(struct wuimg *img, FILE *ifp);

struct wu_st fmt_load_raster_st(struct wuimg *img, FILE *ifp);

size_t fmt_load_raster_callback(struct wuimg *img, FILE *ifp,
fmt_load_callback_t fn, void *restrict ptr);

size_t fmt_load_raster_swap(struct wuimg *img, FILE *ifp, enum endianness e);


enum wu_error fmt_load_pal(FILE *ifp, struct palette *pal,
enum fmt_pal_type type, size_t nmemb);

enum wu_error fmt_sigcmp_mem(const unsigned char *restrict sig, size_t size,
struct mparser *mp);

enum wu_error fmt_sigcmp(const unsigned char *restrict sig, size_t size,
FILE *ifp);

#endif /* COMMON_FMT */

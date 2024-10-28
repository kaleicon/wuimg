// SPDX-License-Identifier: 0BSD
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

size_t fmt_load_raster(struct wuimg *img, FILE *ifp);

size_t fmt_load_raster_swap_depth(struct wuimg *img, FILE *ifp, enum endianness e,
uint8_t word_depth);

size_t fmt_load_raster_swap(struct wuimg *img, FILE *ifp, enum endianness e);

enum wu_error fmt_load_pal(FILE *ifp, struct palette *pal,
enum fmt_pal_type type, size_t entries);

enum wu_error fmt_sigcmp_mem(const unsigned char *restrict sig, size_t size,
struct mparser *mp);

enum wu_error fmt_sigcmp(const unsigned char *restrict sig, size_t size,
FILE *ifp);

#endif /* COMMON_FMT */

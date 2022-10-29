#ifndef COMMON_FMT
#define COMMON_FMT

#include <stdio.h>

#include "raster/wuimg.h"
#include "misc/endian.h"
#include "misc/memparser.h"
#include "raster/pal.h"

enum fmt_pal_type {
	fmt_pal_rgb = 3,
	fmt_pal_rgbx = 4,
};

size_t fmt_load_raster(struct wuimg *img, FILE *ifp, enum endianness e);

enum wu_error fmt_load_pal_planar(FILE *ifp, struct raster_pal *pal,
enum fmt_pal_type type, size_t entries);

enum wu_error fmt_load_pal(FILE *ifp, struct raster_pal *pal,
enum fmt_pal_type type, size_t entries);

enum wu_error fmt_sigcmp_mem(const unsigned char *restrict sig, size_t size,
struct mp_parser *mp);

enum wu_error fmt_sigcmp(const unsigned char *restrict sig, size_t size,
FILE *ifp);

#endif /* COMMON_FMT */

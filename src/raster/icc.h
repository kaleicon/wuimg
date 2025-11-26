// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2022 kaleido
#ifndef RASTER_ICC
#define RASTER_ICC

#include <stdint.h>

#include "raster/alpha.h"

struct icc_transform;
struct icc_profile;
struct icc_file;

uint32_t icc_fmt_colorspace(uint8_t ch, uint8_t bytedepth,
enum alpha_interpretation alpha, uint8_t colorspace);

uint32_t icc_fmt(uint8_t ch, uint8_t bytedepth,
enum alpha_interpretation alpha);


struct wuptr icc_file_get_data(const struct icc_file *icc);

struct icc_transform * icc_file_create_transform(const struct icc_file *icc,
const struct icc_profile *out, uint32_t in_fmt, uint32_t out_fmt);

void icc_file_unref(struct icc_file *icc);

struct icc_file * icc_file_ref(struct icc_file *icc);

struct icc_file * icc_file_mem_own(void *data, size_t size);

struct icc_file * icc_file_mem_copy(const void *data, size_t size);

#endif /* RASTER_ICC */

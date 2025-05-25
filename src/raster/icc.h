// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2022 kaleido
#ifndef RASTER_ICC
#define RASTER_ICC

#include <lcms2.h>
#include <lcms2_plugin.h>

#include "misc/mparser.h"
#include "raster/alpha.h"

struct icc_profile {
	cmsHPROFILE in;
	struct mparser mp;
	struct _cms_io_handler io;
};

cmsUInt32Number icc_fmt_colorspace(uint8_t ch, uint8_t bytedepth,
enum alpha_interpretation alpha, uint8_t colorspace);

cmsUInt32Number icc_fmt(uint8_t ch, uint8_t bytedepth,
enum alpha_interpretation alpha);

void icc_profile_free(struct icc_profile *icc);

bool icc_profile_mem_own(struct icc_profile *icc, void *data, size_t size);

bool icc_profile_mem_copy(struct icc_profile *icc, const void *data,
size_t size);

#endif /* RASTER_ICC */

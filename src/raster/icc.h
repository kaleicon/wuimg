// SPDX-License-Identifier: 0BSD
#ifndef RASTER_ICC
#define RASTER_ICC

#include <lcms2.h>
#include <lcms2_plugin.h>

#include "misc/mparser.h"

struct icc_profile {
	cmsHPROFILE in;
	cmsHTRANSFORM transform;
	struct mparser mp;
	struct _cms_io_handler io;
};

void icc_profile_free(struct icc_profile *icc);

bool icc_profile_mem_copy(struct icc_profile *icc, const void *data,
size_t size);

bool icc_profile_mem_own(struct icc_profile *icc, void *data, size_t size);

#endif /* RASTER_ICC */

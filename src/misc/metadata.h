// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2025 kaleido
#ifndef WU_METADATA
#define WU_METADATA

#include "misc/wutree.h"

enum metadata_type {
	metadata_none = 0,
	metadata_exif,
	metadata_xmp,
	metadata_iptc,
};

unsigned char metadata_orientation(struct wutree *tree);

bool metadata_parse(enum metadata_type type, const void *metadata,
size_t len, struct wutree *tree);

#endif /* WU_METADATA */

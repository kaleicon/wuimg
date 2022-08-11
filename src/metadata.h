#ifndef WU_METADATA
#define WU_METADATA

#include "wutree.h"

enum metadata_type {
	exif_metadata,
	xmp_metadata,
	iptc_metadata,
};

unsigned char metadata_orientation(struct wu_tree *tree);

bool standard_metadata(enum metadata_type type, const void *metadata,
size_t len, struct wu_tree *tree);

#endif /* WU_METADATA */

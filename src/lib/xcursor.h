// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2021 kaleido
#ifndef LIB_XCURSOR
#define LIB_XCURSOR

#include <stdint.h>

#include "raster/wuimg.h"

struct xcursor_image {
	uint32_t xhot;
	uint32_t yhot;
	uint32_t delay;
};

enum xcursor_comment_type {
	xcursor_comment_copyright = 1,
	xcursor_comment_license = 2,
	xcursor_comment_other = 3,
};

struct xcursor_comment {
	enum xcursor_comment_type type;
};

enum xcursor_chunk_type {
	xcursor_chunk_comment = 1,
	xcursor_chunk_image = 2,
};

struct xcursor_chunk {
	long pos;
	size_t len;
	uint32_t type;
	union {
		struct xcursor_comment comment;
		struct xcursor_image image;
	} u;
};

struct xcursor_toc {
	uint32_t type;
	uint32_t subtype;
	uint32_t pos;
};

struct xcursor_desc {
	FILE *ifp;
	uint32_t ntoc;
	uint32_t images;
	uint32_t comments;
	struct xcursor_toc *toc;
};

const char * xcursor_comment_type_str(enum xcursor_comment_type type);

void xcursor_free(struct xcursor_desc *desc);

size_t xcursor_get_chunk_data(const struct xcursor_desc *desc,
const struct xcursor_chunk *chunk, void *restrict dst);

struct wu_st xcursor_get_image_info(const struct xcursor_desc *desc,
const struct xcursor_toc *entry, struct xcursor_chunk *chunk,
struct wuimg *img);

struct wu_st xcursor_get_comment_info(const struct xcursor_desc *desc,
const struct xcursor_toc *entry, struct xcursor_chunk *chunk);

struct wu_st xcursor_parse_header(struct xcursor_desc *desc, FILE *ifp,
uint32_t max_entries);

#endif /* LIB_XCURSOR */

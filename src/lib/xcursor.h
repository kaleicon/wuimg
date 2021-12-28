#ifndef LIB_XCURSOR
#define LIB_XCURSOR

#include <stdint.h>

#include "../raster/lib.h"

struct xcursor_image {
	struct raster_desc r;
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
	uint32_t len;
};

enum xcursor_chunk_type {
	xcursor_chunk_comment = 0xfffe0001,
	xcursor_chunk_image = 0xfffd0002,
};

struct xcursor_chunk {
	enum xcursor_chunk_type type;
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

const char * xcursor_comment_type_string(enum xcursor_comment_type type);

void xcursor_free(struct xcursor_desc *desc);

size_t xcursor_get_chunk_data(struct xcursor_desc *desc,
struct xcursor_chunk *chunk, struct memory *mem);

enum lib_fail xcursor_get_chunk(struct xcursor_desc *desc,
struct xcursor_chunk *chunk, const uint32_t i);

enum lib_fail xcursor_parse_header(struct xcursor_desc *desc,
uint32_t max_entries);

enum lib_fail xcursor_open_file(struct xcursor_desc *desc, FILE *ifp);

#endif /* LIB_XCURSOR */

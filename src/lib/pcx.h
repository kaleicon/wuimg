#ifndef LIB_PCX
#define LIB_PCX

#include <stdio.h>
#include <stdbool.h>

#include "../raster/lib.h"
#include "../raster/pal.h"

enum pcx_version {
	pcx_ver25 = 0,
	pcx_ver28_egapal = 2,
	pcx_ver28_nopal = 3,
	pcx_paintbrush = 4,
	pcx_ver30 = 5,
};

struct pcx_desc {
	FILE *ifp;
	long rle_len;

	struct raster_desc r;
	bool palette_type;
	enum pcx_version version:8;

	uint16_t horz_res, vert_res;
	uint16_t horz_screen, vert_screen;

	uint16_t bytes_per_line;
	unsigned entries;
	unsigned char file_pal[48];
};

const char * pcx_version_string(enum pcx_version ver);

unsigned char * pcx_decode(struct pcx_desc *desc);

enum lib_fail pcx_read_header(struct pcx_desc *desc);

enum lib_fail pcx_open_file(FILE *ifp, struct pcx_desc *desc, long file_len);


struct dcx_desc {
	size_t nr;
	uint32_t off[1024];
	uint32_t len[1024];
};

struct dcx_desc * dcx_read_offsets(FILE *ifp);

enum lib_fail dcx_open_file(FILE *ifp);

#endif /* LIB_PCX */

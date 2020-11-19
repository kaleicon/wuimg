#ifndef LIB_PCX
#define LIB_PCX

#include <stdio.h>
#include <stdbool.h>

#include "common/lib.h"

enum pcx_version {
	pcx_ver25 = 0,
	pcx_ver28_egapal = 2,
	pcx_ver28_nopal = 3,
	pcx_paintbrush = 4,
	pcx_ver30 = 5,
};

struct pcx_desc {
	FILE *ifp;
	unsigned int w, h;
	unsigned int bytes_per_line;
	unsigned char planes, bitdepth;
	enum pcx_version version:8;
	bool palette_type;
	bool cga_mode;
	bool expand_pal;
	unsigned char file_pal[48];
};

unsigned char * pcx_decode(const struct pcx_desc *desc,
unsigned char *restrict *palette);

enum lib_fail pcx_read_header(struct pcx_desc *desc);

enum lib_fail pcx_open_file(FILE *ifp, struct pcx_desc *desc);


struct dcx_desc {
	size_t nr;
	uint32_t off[1024];
	uint32_t len[1024];
};

struct dcx_desc * dcx_read_offsets(FILE *ifp);

enum lib_fail dcx_open_file(FILE *ifp);

#endif /* LIB_PCX */

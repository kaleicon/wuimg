#ifndef LIB_PICT
#define LIB_PICT

#include "common/lib.h"
#include "common/unpack.h"

enum pictor_palette_type {
	no_palette = 0,
	cga_palette = 1,
	pcjr_palette = 2,
	ega_palette = 3,
	vga_palette = 4,
	vga_too_i_think = 5,
};

struct pictor_palette {
	bool enabled;
	enum pictor_palette_type type:16;
	unsigned size;
	struct colormap *pal;
};

struct pictor_desc {
	FILE *ifp;
	unsigned w, h;
	unsigned x, y;
	unsigned char bitdepth, planes;
	char video_mode;
	struct pictor_palette palette;
};

const char * pictor_video_mode(const struct pictor_desc *desc);

void pictor_cleanup(struct pictor_desc *desc);

unsigned char * pictor_decode(const struct pictor_desc *desc);

void * pictor_take_palette(struct pictor_desc *desc);

enum lib_fail pictor_read_header(struct pictor_desc *desc);

enum lib_fail pictor_open_file(FILE *ifp, struct pictor_desc *desc);

#endif /* LIB_PICT */

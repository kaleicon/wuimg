#ifndef LIB_PICT
#define LIB_PICT

#include "../raster/lib.h"

enum pictor_palette_type {
	pictor_no_palette = 0,
	pictor_cga_palette = 1,
	pictor_pcjr_palette = 2,
	pictor_ega_palette = 3,
	pictor_vga_palette = 4,
	pictor_vga_too_i_think = 5,
};

struct pictor_desc {
	FILE *ifp;
	struct raster_desc r;
	uint16_t x, y;
	uint8_t depth;
	uint8_t planes;
	char video_mode;
	enum pictor_palette_type pal_type:8;
	bool has_palette;
	uint16_t blocks;
};

const char * pictor_palette_str(enum pictor_palette_type type);

const char * pictor_video_mode(const struct pictor_desc *desc);

void pictor_cleanup(struct pictor_desc *desc);

size_t pictor_decode(const struct pictor_desc *desc, void *restrict dst);

enum lib_fail pictor_read_header(struct pictor_desc *desc);

enum lib_fail pictor_open_file(struct pictor_desc *desc, FILE *ifp);

#endif /* LIB_PICT */

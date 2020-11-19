#ifndef LIB_PIC
#define LIB_PIC

enum pic_palette_type {
	no_palette = 0,
	cga_palette = 1,
	pcjr_palette = 2,
	ega_palette = 3,
	vga_palette = 4,
};

struct pic_desc {
	FILE *ifp;
	unsigned w, h;
	unsigned x, y;
	unsigned char bpp, planes;
	char video_mode:8;
	enum pic_palette_type palette_type:8;
};

enum lib_fail pic_open_file(FILE *ifp, struct pic_desc *desc);

#endif /* LIB_PIC */

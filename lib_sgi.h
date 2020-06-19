#ifndef LIB_SGI
#define LIB_SGI

#include <stdio.h>

enum sgi_fail {
	sgi_ok = 0,
	sgi_invalid_signature,
	sgi_unexpected_eof,
	sgi_invalid_header,
	sgi_colormap_file,
};

enum sgi_bitmap_type {
	sgi_raw,
	sgi_332,
	sgi_colormap,
	sgi_colormap_define,
};

enum sgi_compression {
	sgi_uncompressed = 0,
	sgi_rle = 1,
};

struct sgi_desc {
	FILE *ifp;
	size_t rle_size;

	unsigned int w, h;
	unsigned char ch;
	unsigned char bytedepth;
	enum sgi_compression compression:8;

	enum sgi_bitmap_type type:8;
	bool swap;
	char name[80];
};

const char * sgi_fail_string(const enum sgi_fail fail);

unsigned char * sgi_decode(const struct sgi_desc *desc);

enum sgi_fail sgi_parse_header(struct sgi_desc *desc);

enum sgi_fail sgi_open_file(FILE *ifp, struct sgi_desc *desc);

#endif /* LIB_SGI */

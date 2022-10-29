#ifndef LIB_MAKI
#define LIB_MAKI

#include "raster/wuimg.h"

enum maki_version {
	maki_1a = 'A',
	maki_1b = 'B',
};

struct maki_desc {
	FILE *ifp;
	uint8_t model[4];
	char comment[20];
	enum maki_version version:8;
	uint16_t x, y;
};

const char * maki_version_str(enum maki_version version);

size_t maki_decode(const struct maki_desc *desc, struct wuimg *img);

enum wu_error maki_parse(struct maki_desc *desc, struct wuimg *img);

enum wu_error maki_open(struct maki_desc *desc, FILE *ifp);

#endif /* LIB_MAKI */

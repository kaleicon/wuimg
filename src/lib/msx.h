#ifndef LIB_MSX
#define LIB_MSX

#include <stdio.h>

#include "wudefs.h"

enum msx_screen {
	msx_screen2,
	msx_screen3,
	msx_screen4,
	msx_screen5,
	msx_screen6,
	msx_screen7,
	msx_screen8,
	msx_screen10,
	msx_screen12,
};

struct msx_desc {
	FILE *ifp;
	enum msx_screen mode;
	uint16_t end;
};

size_t msx_decode(const struct msx_desc *desc, struct raw_img *img);

enum wu_error msx_parse(struct msx_desc *desc, struct raw_img *img, FILE *ifp,
enum msx_screen mode);

#endif /* LIB_MSX */

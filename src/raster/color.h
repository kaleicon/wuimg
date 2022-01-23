#ifndef WU_COLOR
#define WU_COLOR

#include "pix.h"

enum color_space {
	color_space_rgb,
	color_space_ycbcr,
	color_space_ycbcr_limited,
};

struct color_mat {
	float off[4];
	float mat[4*4];
};

const char * color_space_str(enum color_space cs);

void color_mat_gen(struct color_mat *cs, enum color_space space,
enum pix_layout swizzle);

#endif /* WU_COLOR */

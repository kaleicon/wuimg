#ifndef COLORIMETRY
#define COLORIMETRY

#include "wudefs.h"

int get_image_color(float out[static 3], const struct raw_img *img,
enum background_source src, size_t maxres);

#endif /* COLORIMETRY */

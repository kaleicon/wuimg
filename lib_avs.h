#ifndef LIB_AVS
#define LIB_AVS

#include <stdio.h>

#include "common_lib.h"

uint8_t *avs_load(FILE *ifp, size_t width, size_t height);

enum lib_fail avs_open_file(FILE *ifp, size_t *width, size_t *height);

#endif /* LIB_AVS */

#ifndef LIB_AVS
#define LIB_AVS

#include <stdio.h>

#include "../raster/lib.h"

enum lib_fail avs_open_file(struct raster_desc *desc, FILE *ifp);

#endif /* LIB_AVS */

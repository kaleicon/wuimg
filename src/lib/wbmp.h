#ifndef LIB_WBMP
#define LIB_WBMP

#include <stdio.h>

#include "../raster/lib.h"

enum lib_fail wbmp_open_file(struct raster_desc *desc, FILE *ifp);

#endif /* LIB_WBMP */

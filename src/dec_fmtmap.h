#ifndef WU_FMTMAP
#define WU_FMTMAP

#include <stdio.h>
#include <stdbool.h>

#include "wustr.h"

int fmtmap_identify_file(FILE *ifp, const char *filename);

bool fmtmap_known_extension(const struct wuptr filename);

void fmtmap_print_data(void);

#endif /* WU_FMTMAP */

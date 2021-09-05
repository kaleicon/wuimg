#ifndef DEC_FMTMAP
#define DEC_FMTMAP

#include <stdio.h>
#include <stdbool.h>

#include "wustr.h"
#include "dec_enable.def"

enum format_id {
	fmt_unknown = -1,
#define WUDEC(name, callback) fmt_##name,
#include "dec.def"
#undef WUDEC
};

bool known_extension(const struct wustr filename);

enum format_id identify_image(FILE *ifp, const char *filename);

void print_map_data(void);

#endif /* DEC_FMTMAP */

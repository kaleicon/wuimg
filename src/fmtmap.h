// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2024 kaleido
#ifndef FMTMAP
#define FMTMAP

#include <stdbool.h>

#include "wudefs.h"

const struct fmt_desc * fmtmap_identify(struct image_file *file,
const char *name);

const struct fmt_desc * fmtmap_by_name(const char *name);

bool fmtmap_known_extension(const struct wuptr filename);

void fmtmap_print_known(FILE *ofp);

#endif /* FMTMAP */

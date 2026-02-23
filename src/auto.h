// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2024 kaleido
#ifndef WUAUTO
#define WUAUTO

#include "wudefs.h"
#include "misc/endian.h"

struct wu_st auto_load(struct image_file *infile);

struct wu_st auto_init(struct image_file *infile, const struct wuptr desc);

#endif /* WUAUTO */

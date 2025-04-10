// SPDX-License-Identifier: 0BSD
#ifndef WUAUTO
#define WUAUTO

#include "wudefs.h"
#include "misc/endian.h"

enum wu_error auto_load(struct image_file *infile);

struct wu_st auto_init(struct image_file *infile, const struct wu_conf *conf,
const struct wuptr desc);

#endif /* WUAUTO */

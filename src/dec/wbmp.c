// SPDX-License-Identifier: 0BSD
#include "rast_utils.h"
#include "lib/wbmp.h"

static enum wu_error wbmp_dec(struct image_file *infile,
const struct wu_conf *wuconf) {
	return rast_trivial_fread(infile, wuconf, wbmp_open_file);
}

const struct image_fn wbmp_fn = {.dec = wbmp_dec};

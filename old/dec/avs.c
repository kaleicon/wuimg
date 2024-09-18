// SPDX-License-Identifier: 0BSD
#include "rast_utils.h"
#include "lib/avs.h"

static enum wu_error avs_dec(struct image_file *infile,
const struct wu_conf *wuconf) {
	return rast_trivial_fread(infile, wuconf, avs_open_file);
}

const struct image_fn avs_fn = {.dec = avs_dec};

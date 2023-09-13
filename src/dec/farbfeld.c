// SPDX-License-Identifier: 0BSD
#include "rast_utils.h"
#include "lib/farbfeld.h"

enum wu_error farbfeld_dec(struct image_file *infile,
const struct wu_conf *wuconf) {
	return rast_trivial_fread(infile, wuconf, farbfeld_open_file);
}

const struct image_fn farbfeld_fn = {.dec = farbfeld_dec};

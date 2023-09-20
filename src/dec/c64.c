// SPDX-License-Identifier: 0BSD
#include "rast_utils.h"
#include "lib/c64.h"

static size_t dec(const void *restrict mp, struct wuimg *img) {
	return c64_decode(mp, img);
}
static enum wu_error parse(void *restrict mp, struct wuimg *img) {
	return c64_guess(mp, img);
}

static enum wu_error c64_dec(struct image_file *infile,
const struct wu_conf *wuconf) {
	struct mp_parser mp = mp_parser_map(infile->map);
	return rast_trivial_opened(infile, wuconf, &mp, parse, NULL, dec, NULL);
}

const struct image_fn c64_fn = {.mmap = true, .dec = c64_dec};

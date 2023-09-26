// SPDX-License-Identifier: 0BSD
#include "rast_utils.h"
#include "lib/xyz.h"

static size_t dec(const void *restrict mp, struct wuimg *img) {
	return xyz_decode(mp, img);
}
static enum wu_error parse(void *restrict mp, struct wuimg *img) {
	return xyz_parse(mp, img);
}

static enum wu_error xyz_dec(struct image_file *infile,
const struct wu_conf *wuconf) {
	struct mparser mp = mp_map(infile->map);
	enum wu_error st = xyz_open(&mp);
	if (st == wu_ok) {
		return rast_trivial_opened(infile, wuconf, &mp, parse, NULL,
			dec, NULL);
	}
	return st;
}

const struct image_fn xyz_fn = {.mmap = true, .dec = xyz_dec};

// SPDX-License-Identifier: 0BSD
#include "rast_utils.h"
#include "lib/xyz.h"

static size_t dec(const void *restrict mp, struct wuimg *img) {
	return xyz_decode(mp, img);
}
static enum wu_error parse(void *restrict mp, struct wuimg *img) {
	return xyz_parse(mp, img);
}
static enum wu_error init(void *restrict mp, struct image_file *infile) {
	return xyz_init(mp, infile->map);
}

static enum wu_error xyz_dec(struct image_file *infile,
const struct wu_conf *conf) {
	struct mparser mp;
	return rast_trivial_dec(infile, conf, &mp, init, parse, NULL, dec);
}

const struct image_fn xyz_fn = {
	.mmap = true,
	.dec = xyz_dec,
};

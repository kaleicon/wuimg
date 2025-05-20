// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2022 kaleido
#include "rast_utils.h"
#include "lib/qoi.h"

static size_t dec(const void *restrict desc, struct wuimg *img) {
	const struct mparser *mp = desc;
	return qoi_decode(*mp, img);
}
static enum wu_error parse(void *restrict desc, struct wuimg *img) {
	return qoi_parse(desc, img);
}
static enum wu_error init(void *restrict desc, struct image_file *infile) {
	return qoi_init(desc, infile->map);
}

static enum wu_error qoi_dec(struct image_file *infile,
const struct wu_conf *wuconf) {
	struct mparser mp;
	return rast_trivial_dec(infile, wuconf, &mp, init, parse, NULL, dec);
}

const struct image_fn qoi_fn = {
	.mmap = true,
	.dec = qoi_dec,
};

// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2025 kaleido
#include "rast_utils.h"
#include "lib/jam.h"

static size_t dec(const void *restrict ptr, struct wuimg *img) {
	const struct mparser *mp = ptr;
	return jam_decode(*mp, img);
}
static enum wu_error parse(void *restrict ptr, struct wuimg *img) {
	return jam_parse(ptr, img);
}
static enum wu_error init(void *restrict ptr, struct image_file *infile) {
	return jam_identify(ptr, infile->map);
}

static enum wu_error jam_dec(struct image_file *infile,
const struct wu_conf *conf) {
	struct mparser mp;
	return rast_trivial_dec(infile, conf, &mp, init, parse, NULL, dec);
}

const struct image_fn jam_fn = {
	.mmap = true,
	.dec = jam_dec,
};

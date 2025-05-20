// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2020 kaleido
#include "rast_utils.h"
#include "lib/sixel.h"

static size_t dec(const void *ptr, struct wuimg *img) {
	return sixel_decode(ptr, img);
}
static enum wu_error parse(void *ptr, struct wuimg *img) {
	return sixel_calc_parameters(ptr, img);
}
static enum wu_error open(void *ptr, struct image_file *infile) {
	return sixel_open_mem(ptr, infile->map);
}

static enum wu_error sixel_dec(struct image_file *infile,
const struct wu_conf *conf) {
	struct sixel_desc desc;
	return rast_trivial_dec(infile, conf, &desc, open, parse, NULL, dec);
}

const struct image_fn sixel_fn = {.mmap = true, .dec = sixel_dec};

// SPDX-License-Identifier: 0BSD
#include "rast_utils.h"
#include "lib/qoi.h"

static size_t dec(const void *restrict desc, struct wuimg *img) {
	return qoi_decode(desc, img);
}
static enum wu_error parse(void *restrict desc, struct wuimg *img) {
	return qoi_parse(desc, img);
}

static enum wu_error qoi_dec(struct image_file *infile,
const struct wu_conf *wuconf) {
	struct mparser mp = mp_map(infile->map);
	const enum wu_error st = qoi_open(&mp);
	return (st == wu_ok)
		? rast_trivial_opened(infile, wuconf, &mp, parse, NULL, dec, NULL)
		: st;
}

const struct image_fn qoi_fn = {.mmap = true, .dec = qoi_dec};

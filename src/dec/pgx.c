// SPDX-License-Identifier: 0BSD
#include "rast_utils.h"
#include "lib/pgx.h"

static size_t dec(const void *restrict desc, struct wuimg *img) {
	return pgx_decode(desc, img);
}
static enum wu_error parse(void *restrict desc, struct wuimg *img) {
	return pgx_read_header(desc, img);
}
static enum wu_error open(void *restrict desc, struct image_file *infile) {
	return pgx_open_file(desc, infile->ifp);
}

static enum wu_error pgx_dec(struct image_file *infile,
const struct wu_conf *wuconf) {
	struct pgx_desc desc;
	return rast_trivial_dec(infile, wuconf, &desc, open, parse, NULL, dec,
		NULL);
}

const struct image_fn pgx_fn = {.dec = pgx_dec};

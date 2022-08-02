#include <stdlib.h>
#include <string.h>

#include "wudefs.h"
#include "rast_utils.h"
#include "lib/sixel.h"

static size_t dec(const void *ptr, struct raw_img *img) {
	return sixel_decode(ptr, img);
}
static enum wu_error parse(void *ptr, struct raw_img *img) {
	return sixel_calc_parameters(ptr, img);
}
static enum wu_error mopen(void *ptr, const struct mp_parser mp) {
	return sixel_open_mem(ptr, mp);
}

enum wu_error sixel_dec(struct image_file *infile, const struct wu_conf *conf) {
	struct sixel_desc desc;
	return rast_trivial_map(infile, conf, &desc, mopen, parse, NULL, dec,
		NULL);
}

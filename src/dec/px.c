#include "wudefs.h"
#include "misc/math.h"
#include "lib/px.h"

enum wu_error px_callback(struct image_file *infile,
const struct wu_conf *wuconf, struct wu_state *state, const enum image_event ev) {
	(void)wuconf;
	if (ev) {
		const uint32_t idx = (uint32_t)state->idx;
		return px_decode(infile->dec_state, infile->sub_img + idx, idx);
	}
	return wu_no_change;
}

enum wu_error px_dec(struct image_file *infile, const struct wu_conf *wuconf) {
	struct px_desc *desc = malloc(sizeof(*desc));
	if (!desc) {
		return wu_alloc_error;
	}
	infile->dec_state = desc;
	infile->events = ev_subcycle;

	const enum wu_error err = px_parse(desc, infile->ifp);
	if (err != wu_ok) {
		return err;
	}

	if (umax(desc->w, desc->h) > wuconf->max_img_size) {
		return wu_exceeds_size_limit;
	}

	struct wuimg *img = alloc_sub_images(infile, desc->nr);
	if (!img) {
		return wu_alloc_error;
	}

	return px_decode(desc, img, 0);
}

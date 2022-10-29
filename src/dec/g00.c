#include "lib/g00.h"
#include "wudefs.h"

enum wu_error g00_callback(struct image_file *infile,
const struct wu_conf *wuconf, struct wu_state *state,
const enum image_event ev) {
	(void)wuconf; (void)state; (void)ev;
	g00_cleanup(infile->dec_state, infile->sub_img);
	return wu_ok;
}

static enum wu_error decode(struct image_file *infile,
const struct wu_conf *wuconf, struct g00_desc *desc) {
	struct wuimg *img = alloc_sub_images(infile, 1);
	if (!img) {
		return wu_alloc_error;
	}

	const enum wu_error st = g00_read_header(desc, img, infile->ifp);
	if (st != wu_ok) {
		return st;
	}

	if (wuimg_exceeds_limit(img, wuconf)) {
		return wu_exceeds_size_limit;
	}

	tree_bud_leaf(&infile->metadata, "Version",
		(struct wu_leaf){.val.u = desc->version, .type = wu_leaf_unsigned});
	return g00_decode(desc, img) ? wu_ok : wu_decoding_error;
}

enum wu_error g00_dec(struct image_file *infile, const struct wu_conf *wuconf) {
	struct g00_desc *desc = malloc(sizeof(*desc));
	if (desc) {
		const enum wu_error st = decode(infile, wuconf, desc);
		if (st == wu_ok) {
			infile->dec_state = desc;
		} else {
			g00_cleanup(desc, infile->sub_img);
			free(desc);
		}
		return st;
	}
	return wu_alloc_error;
}

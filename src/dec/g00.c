#include <stdlib.h>

#include "g00.h"
#include "../lib/g00.h"
#include "../rast_utils.h"

enum wu_error g00_callback(struct image_file *infile,
const struct wu_conf *wuconf, struct wu_state *state,
const enum image_event ev) {
	(void)wuconf;
	(void)state;
	if (ev == ev_end) {
		for (size_t i = 0; i < infile->nr; ++i) {
			infile->sub_img[i].data = NULL;
		}
		g00_cleanup(infile->dec_state);
		free(infile->dec_state);
	}
	return wu_ok;
}

static enum wu_error decode(struct image_file *infile,
const struct wu_conf *wuconf, struct g00_desc *desc) {
	const enum lib_fail st = g00_read_header(desc, infile->ifp);
	if (st != lib_ok) {
		rast_error(infile, st);
		return wu_invalid_header;
	}

	if (rast_exceeds_size(&desc->r, wuconf)) {
		return wu_exceeds_size_limit;
	}

	tree_bud_leaf(&infile->metadata, "Version",
		(struct wu_leaf){.val.u = desc->version, .type = wu_leaf_unsigned});

	struct raw_img *img = alloc_sub_images(infile, 1);
	if (!img) {
		return wu_alloc_error;
	}

	const size_t w = g00_decode(desc);
	if (!w) {
		return wu_decoding_error;
	}

	rast_to_raw(img, &desc->r);
	img->data = desc->pix_data;
	return wu_ok;
}

enum wu_error g00_dec(struct image_file *infile, const struct wu_conf *wuconf) {
	struct g00_desc *desc = malloc(sizeof(*desc));
	if (desc) {
		const enum wu_error st = decode(infile, wuconf, desc);
		if (st == wu_ok) {
			infile->dec_state = desc;
		} else {
			g00_cleanup(desc);
			free(desc);
		}
		return st;
	}
	return wu_alloc_error;
}

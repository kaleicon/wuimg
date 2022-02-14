#include <string.h>

#include "../wudefs.h"
#include "../wutree.h"
#include "../common.h"
#include "../rast_utils.h"
#include "../lib/wbm.h"

static enum wu_error decode(struct image_file *infile,
const struct wu_conf *wuconf, struct wbm_desc *desc) {
	const enum lib_fail status = wbm_parse_header(desc);
	if (status) {
		rast_error(infile, status);
		return wu_invalid_header;
	}

	struct wu_tree_sap sap[3] = {
		{"Depth", wu_leaf_unsigned, {.u = desc->depth}},
	};
	tree_bud_leaves(&infile->metadata, sap, 1);

	if (rast_exceeds_size(&desc->r, wuconf)) {
		return wu_exceeds_size_limit;
	}

	struct raw_img *img = alloc_sub_images(infile, 1);
	if (!img) {
		return wu_alloc_error;
	}

	if (!rast_to_raw_img(&desc->r, img)) {
		return wu_alloc_error;
	}
	return wbm_decode(desc, img->data) ? wu_ok : wu_decoding_error;
}

enum wu_error wbm_dec(struct image_file *infile, const struct wu_conf *wuconf) {
	struct wbm_desc desc;
	const enum lib_fail fail = wbm_open_file(&desc, infile->ifp);
	if (fail == lib_ok) {
		const enum wu_error s = decode(infile, wuconf, &desc);
		wbm_cleanup(&desc);
		return s;
	}
	rast_error(infile, fail);
	return wu_open_error;
}

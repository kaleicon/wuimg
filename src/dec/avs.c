#include "../wudefs.h"
#include "../common.h"
#include "../rast_utils.h"
#include "../lib/avs.h"

enum wu_error avs_dec(struct image_file *infile, const struct wu_conf *wuconf) {
	struct raster_desc desc;
	const enum lib_fail status = avs_open_file(&desc, infile->ifp);
	if (status != lib_ok) {
		return wu_unexpected_eof;
	}

	if (rast_exceeds_size(&desc, wuconf)) {
		return wu_exceeds_size_limit;
	}

	struct raw_img *img = alloc_sub_images(infile, 1);
	if (img) {
		img->data = lib_load_rast(infile->ifp, &desc);
		if (img->data) {
			rast_to_raw(img, &desc);
			return wu_ok;
		}
	}
	return wu_alloc_error;
}

#include <string.h>

#include "../wudefs.h"
#include "../rast_utils.h"
#include "../lib/wbmp.h"

enum wu_error wbmp_dec(struct image_file *infile,
const struct wu_conf *wuconf) {
	struct raster_desc desc;
	const enum lib_fail status = wbmp_open_file(&desc, infile->ifp);
	if (status != lib_ok) {
		infile->err_msg = strdup(lib_fail_string(status));
		return wu_invalid_header;
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

#include <string.h>

#include "../wudefs.h"
#include "../common.h"
#include "lib/wbmp.h"

enum wu_error wbmp_dec(struct image_file *infile,
const struct wu_conf *wuconf) {
	struct wbmp_desc desc;
	const enum lib_fail status = wbmp_open_file(infile->ifp, &desc);
	if (status != lib_ok) {
		infile->err_msg = strdup(lib_fail_string(status));
		return wu_invalid_header;
	}

	if (zumax(desc.w, desc.h) > wuconf->max_img_size) {
		return wu_exceeded_size_limit;
	}

	struct raw_img *img = alloc_sub_images(infile, 1);
	if (!img) {
		return wu_alloc_error;
	}

	const bool expand = false;
	img->data = wbmp_decode(&desc, expand);
	if (!img->data) {
		return wu_alloc_error;
	}

	img->w = desc.w;
	img->h = desc.h;
	img->channels = 1;
	img->bitdepth = expand ? 8 : 1;
	return wu_ok;
}

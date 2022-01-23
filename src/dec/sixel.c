#include <stdlib.h>
#include <string.h>

#include "../wudefs.h"
#include "../common.h"
#include "../rast_utils.h"
#include "../lib/sixel.h"

static enum wu_error decode(struct image_file *infile,
const struct wu_conf *conf, const struct map_info *mm) {
	struct sixel_desc desc;
	enum lib_fail status = sixel_open_mem(&desc, mm);
	if (status != lib_ok) {
		rast_error(infile, status);
		return wu_open_error;
	}

	status = sixel_calc_parameters(&desc);
	if (status != lib_ok) {
		rast_error(infile, status);
		return wu_invalid_header;
	}

	if (rast_exceeds_size(&desc.r, conf)) {
		return wu_exceeds_size_limit;
	}

	struct raw_img *img = alloc_sub_images(infile, 1);
	if (!img) {
		return wu_alloc_error;
	}

	rast_to_raw(img, &desc.r);
	img->data = calloc(raster_size(&desc.r), 1);
	if (!img->data) {
		return wu_alloc_error;
	}

	return sixel_decode(&desc, (struct pix_rgba8 *)img->data)
		? wu_ok : wu_decoding_error;
}

enum wu_error sixel_dec(struct image_file *infile, const struct wu_conf *conf) {
	struct map_info mm;
	if (!map_file(&mm, infile->ifp)) {
		return wu_alloc_error;
	}

	const enum wu_error st = decode(infile, conf, &mm);
	unmap_file(&mm);
	return st;
}

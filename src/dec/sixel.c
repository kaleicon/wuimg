#include <string.h>

#include "../wudefs.h"
#include "../common.h"
#include "../rast_utils.h"
#include "../lib/sixel.h"

enum wu_error sixel_dec(struct image_file *infile, const struct wu_conf *conf) {
	struct mmap_info mm;
	if (!mmap_file(&mm, infile->ifp)) {
		return wu_alloc_error;
	}

	struct sixel_desc desc;
	enum lib_fail status = sixel_open_mem(&desc, &mm);
	if (status != lib_ok) {
		munmap_file(mm);
		rast_error(infile, status);
		return wu_open_error;
	}

	status = sixel_calc_parameters(&desc);
	if (status != lib_ok) {
		munmap_file(mm);
		rast_error(infile, status);
		return wu_invalid_header;
	}

	if (rast_exceeds_size(&desc.r, conf)) {
		munmap_file(mm);
		return wu_exceeds_size_limit;
	}

	struct raw_img *img = alloc_sub_images(infile, 1);
	if (!img) {
		munmap_file(mm);
		return wu_alloc_error;
	}

	rast_to_raw(img, &desc.r);
	img->data = (unsigned char *)sixel_decode(&desc);
	munmap_file(mm);
	if (!img->data) {
		return wu_alloc_error;
	}
	return wu_ok;
}

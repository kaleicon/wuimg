#include "../wudefs.h"
#include "../common.h"
#include "../rast_utils.h"
#include "../lib/tlg.h"

static enum wu_error decode(struct image_file *infile,
const struct wu_conf *wuconf, const struct map_info *mm) {
	struct tlg_desc desc;
	enum lib_fail st = tlg_open_mem(&desc, mm);
	if (st != lib_ok) {
		rast_error(infile, st);
		return wu_invalid_signature;
	}

	st = tlg_read_header(&desc);
	if (st != lib_ok) {
		rast_error(infile, st);
		return wu_invalid_header;
	}

	if (rast_exceeds_size(&desc.r, wuconf)) {
		return wu_exceeds_size_limit;
	}

	tree_sprout_leaf(&infile->metadata, "Version",
		tlg_version_str(desc.version));

	struct raw_img *img = alloc_sub_images(infile, 1);
	if (!img) {
		return wu_alloc_error;
	}

	if (!rast_to_raw_img(&desc.r, img)) {
		return wu_alloc_error;
	}
	return tlg_decode(&desc, img->data) ? wu_ok : wu_decoding_error;
}

enum wu_error tlg_dec(struct image_file *infile, const struct wu_conf *wuconf) {
	struct map_info mm;
	if (map_file(&mm, infile->ifp)) {
		const enum wu_error err = decode(infile, wuconf, &mm);
		unmap_file(&mm);
		return err;
	}
	return wu_open_error;
}

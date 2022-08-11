#include <stdlib.h>

#include "rast_utils.h"
#include "raster/fmt.h"

static enum wu_error common_trivial(struct image_file *infile,
const struct wu_conf *wuconf, void *desc, rast_vparse_t parse,
rast_vmeta_t meta, rast_vdec_t dec, rast_vfree_t cleanup) {
	struct raw_img *img = alloc_sub_images(infile, 1);
	enum wu_error st = wu_alloc_error;
	if (img) {
		st = (*parse)(desc, img);
		if (st == wu_ok) {
			if (meta) {
				(*meta)(desc, &infile->metadata);
			}
			if (raw_img_exceeds_limit(img, wuconf)) {
				st = wu_exceeds_size_limit;
			} else {
				st = (*dec)(desc, img)
					? wu_ok : wu_decoding_error;
			}
		}
		if (cleanup) {
			(*cleanup)(desc);
		}
	}
	return st;
}

enum wu_error rast_trivial_map(struct image_file *infile,
const struct wu_conf *wuconf, void *desc, rast_vmopen_t mopen,
rast_vparse_t parse, rast_vmeta_t meta, rast_vdec_t dec, rast_vfree_t cleanup) {
	struct map_info mm;
	enum wu_error st = wu_open_error;
	if (map_file(&mm, infile->ifp)) {
		st = (*mopen)(desc, mp_parser_mem(mm.len, mm.data));
		if (st == wu_ok) {
			st = common_trivial(infile, wuconf, desc, parse, meta,
				dec, cleanup);
		}
		unmap_file(&mm);
	}
	return st;
}

enum wu_error rast_trivial_dec(struct image_file *infile,
const struct wu_conf *wuconf, void *desc, rast_vopen_t open,
rast_vparse_t parse, rast_vmeta_t meta, rast_vdec_t dec, rast_vfree_t cleanup) {
	enum wu_error st = (*open)(desc, infile->ifp);
	if (st == wu_ok) {
		st = common_trivial(infile, wuconf, desc, parse, meta, dec,
			cleanup);
	}
	return st;
}

enum wu_error rast_map_wrap(struct image_file *infile,
const struct wu_conf *wuconf, rast_map_t wrap_fn) {
	struct map_info mm;
	enum wu_error err = wu_open_error;
	if (map_file(&mm, infile->ifp)) {
		err = (*wrap_fn)(infile, wuconf, &mm);
		unmap_file(&mm);
	}
	return err;
}

enum wu_error rast_fread_dec(struct image_file *infile,
const struct wu_conf *wuconf, rast_open_t open_fn) {
	struct raw_img *img = alloc_sub_images(infile, 1);
	if (img) {
		enum wu_error st = (*open_fn)(img, infile->ifp);
		if (st == wu_ok) {
			if (!raw_img_exceeds_limit(img, wuconf)) {
				st = raw_img_alloc(img);
				if (st == wu_ok) {
					return fmt_load_raster(img, infile->ifp,
						big_endian)
						? wu_ok : wu_unexpected_eof;
				}
				return st;
			}
			return wu_exceeds_size_limit;
		}
		return st;
	}
	return wu_alloc_error;
}

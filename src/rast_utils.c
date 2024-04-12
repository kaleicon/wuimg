// SPDX-License-Identifier: 0BSD
#include <stdlib.h>

#include "raster/fmt.h"
#include "rast_utils.h"

enum wu_error rast_trivial_opened(struct image_file *infile,
const struct wu_conf *wuconf, void *desc, rast_vparse_t parse,
rast_vmeta_t meta, rast_vdec_t dec, rast_vfree_t cleanup) {
	struct wuimg *img = alloc_sub_images(infile, 1);
	enum wu_error st = wu_alloc_error;
	if (img) {
		st = (*parse)(desc, img);
		if (st == wu_ok) {
			if (meta) {
				(*meta)(desc, &infile->metadata);
			}
			if (wuimg_exceeds_limit(img, wuconf)) {
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

enum wu_error rast_trivial_dec(struct image_file *infile,
const struct wu_conf *wuconf, void *desc, rast_vopen_t open,
rast_vparse_t parse, rast_vmeta_t meta, rast_vdec_t dec, rast_vfree_t cleanup) {
	enum wu_error st = (*open)(desc, infile);
	if (st == wu_ok) {
		st = rast_trivial_opened(infile, wuconf, desc, parse, meta,
			dec, cleanup);
	}
	return st;
}

enum wu_error rast_trivial_fread(struct image_file *infile,
const struct wu_conf *wuconf, rast_open_t open_fn) {
	struct wuimg *img = alloc_sub_images(infile, 1);
	if (img) {
		enum wu_error st = (*open_fn)(img, infile->ifp);
		if (st == wu_ok) {
			if (!wuimg_exceeds_limit(img, wuconf)) {
				st = wuimg_alloc(img);
				if (st == wu_ok) {
					return fmt_load_raster_swap(img,
						infile->ifp, big_endian)
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

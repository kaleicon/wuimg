// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2021 kaleido
#include "raster/fmt.h"
#include "rast_utils.h"

enum wu_error rast_trivial_dec(struct image_file *infile,
const struct wu_conf *conf, void *desc, rast_vopen_t open,
rast_vparse_t parse, rast_vmeta_t meta, rast_vdec_t dec) {
	enum wu_error st = (*open)(desc, infile);
	if (st == wu_ok) {
		struct wuimg *img = alloc_sub_images(infile, 1);
		if (img) {
			st = (*parse)(desc, img);
			if (st == wu_ok) {
				if (meta) {
					(*meta)(desc, &infile->metadata);
				}
				if (wuimg_exceeds_limit(img, conf)) {
					st = wu_exceeds_size_limit;
				} else {
					st = (*dec)(desc, img)
						? wu_ok : wu_decoding_error;
				}
			}
		} else {
			st = wu_alloc_error;
		}
	}
	return st;
}

enum wu_error rast_trivial_fread(struct image_file *infile,
const struct wu_conf *conf, rast_open_t open_fn) {
	struct wuimg *img = infile->sub_img;
	enum wu_error st = (*open_fn)(img, infile->ifp);
	if (st == wu_ok) {
		if (!wuimg_exceeds_limit(img, conf)) {
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

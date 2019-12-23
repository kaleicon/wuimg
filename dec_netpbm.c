#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

#include "wudefs.h"
#include "lib_netpbm.h"

enum wu_error_type netpbm_dec(struct image_file *infile) {
	struct pnm_desc desc;
	if (!open_pnm_file(infile->name, &desc)) {
		return wu_open_error;
	}

	errno = 0;
	if (!parse_pnm_header(&desc)) {
		infile->err_msg = strdup(strerror(errno));
		return wu_invalid_header;
	}

	struct raw_img *img = alloc_sub_images(infile, desc.nr);
	size_t i = 0;
	while (i < infile->nr) {
		img[i].data = decode_pnm_next(&desc);
		if (!img[i].data) {
			break;
		}
		img[i].w = (unsigned int)desc.w;
		img[i].h = (unsigned int)desc.h;
		img[i].channels = (unsigned char)desc.ch;
		img[i].bitdepth = (unsigned char)(desc.depth * 8);
		++i;
	}

	close_pnm_file(&desc);
	if (!i) {
		return wu_decoding_error;
	} else if (i < infile->nr) {
		fit_sub_images(infile, i);
	}
	return wu_ok;
}


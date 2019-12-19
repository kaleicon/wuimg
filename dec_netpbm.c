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

	size_t decoded;
	unsigned char **hold = decode_pnm_file(&desc, &decoded);
	close_pnm_file(&desc);
	if (decoded) {
		struct raw_img *img = alloc_sub_images(infile, decoded);
		for (size_t i = 0; i < decoded; ++i) {
			img[i].data = hold[i];
			img[i].w = (unsigned int)desc.w;
			img[i].h = (unsigned int)desc.h;
			img[i].channels = (unsigned char)desc.ch;
			img[i].bitdepth = (unsigned char)(desc.depth * 8);
		}
		free(hold);
		return wu_ok;
	}
	return wu_decoding_error;
}


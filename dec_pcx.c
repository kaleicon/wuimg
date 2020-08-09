#include "common.h"
#include "wudefs.h"

#include "lib_pcx.h"

enum wu_error common_pcx(FILE *ifp, struct raw_img *img, FILE *metadata,
const unsigned int max_img_size) {
	struct pcx_desc desc;
	enum lib_fail status = pcx_open_file(ifp, &desc);
	if (status != lib_ok) {
		return wu_unknown_file_type;
	}

	status = pcx_read_header(&desc);
	if (status != lib_ok) {
		return wu_invalid_header;
	}

	if (metadata) {
		const char *version;
		switch (desc.version) {
		case pcx_ver25: version = "2.5"; break;
		case pcx_ver28_egapal: version = "2.8 with palette"; break;
		case pcx_ver28_nopal: version = "2.8 without palette"; break;
		case pcx_paintbrush: version = "for PC"; break;
		case pcx_ver30: version = "3.0"; break;
		default: version = "???"; break;
		}

		fprintf(metadata,
			"Format version: %u (%s)\n"
			"Bits per pixel: %d\n"
			"Planes: %d\n",
			desc.version, version, desc.bitdepth, desc.planes);
	}

	if (umax(desc.w, desc.h) > max_img_size) {
		return wu_exceeded_size_limit;
	}

	desc.expand_pal = false;
	desc.cga_mode = false;
	img->w = desc.w;
	img->h = desc.h;
	img->channels = desc.planes;
	img->bitdepth = 8;
	img->data = pcx_decode(&desc, &img->palette);
	return img->data ? wu_ok : wu_decoding_error;
}

enum wu_error pcx_dec(struct image_file *infile, const struct wu_conf *wuconf) {
	struct raw_img *img = alloc_sub_images(infile, 1);
	if (!img) {
		return wu_alloc_error;
	}

	return common_pcx(infile->ifp, img, infile->meta.fp, wuconf->max_img_size);
}

enum wu_error dcx_dec(struct image_file *infile, const struct wu_conf *wuconf) {
	const enum lib_fail status = dcx_open_file(infile->ifp);
	if (status != lib_ok) {
		return wu_unknown_file_type;
	}

	struct dcx_desc *desc = dcx_read_offsets(infile->ifp);
	if (!desc) {
		return wu_alloc_error;
	} else if (!desc->nr) {
		return wu_unexpected_eof;
	}

	size_t max = 0;
	for (size_t i = 0; i < desc->nr; ++i) {
		max = zumax(max, desc->len[i]);
	}
	if (max <= 128) {
		free(desc);
		return wu_unexpected_eof;
	}

	struct raw_img *img = alloc_sub_images(infile, desc->nr);
	if (!img) {
		free(desc);
		return wu_alloc_error;
	}

	unsigned char *buf = malloc(max);
	if (!buf) {
		free(desc);
		return wu_alloc_error;
	}

	size_t i = 0;
	for (size_t idx = 0; idx < desc->nr; ++idx) {
		fseek(infile->ifp, (long)desc->off[i], SEEK_SET);
		const size_t read = fread(buf, 1, desc->len[i], infile->ifp);
		if (read <= 128) {
			continue;
		}

		FILE *pcx = fmemopen(buf, desc->len[i], "rb");
		if (!pcx) {
			continue;
		}

		common_pcx(pcx, img + i, NULL, wuconf->max_img_size);
		fclose(pcx);
		++i;
	}
	free(buf);
	free(desc);

	if (!i) {
		return wu_decoding_error;
	} else if (i < desc->nr) {
		realloc_sub_images(infile, i);
	}
	return wu_ok;
}

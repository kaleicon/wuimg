#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib_pi.h"

#include "wudefs.h"
#include "common.h"

static void print_pi_info(struct pi_decompress_info *pinfo) {
	printf("width = %hu, height = %hu,\n"
		"palette mode = %hhu, plane number = %hhu,\n"
		"screen ratio numerator = %hhu, "
		"screen ratio denominator = %hhu,\n",
		pinfo->width, pinfo->height, pinfo->palette_mode,
		pinfo->nr_of_planes, pinfo->screen_ratio_num,
		pinfo->screen_ratio_den);
	printf("\nComment:\n");
	print_unsafe_data(pinfo->comment, pinfo->comment_len);
	printf("\nDummy bytes:\n");
	print_unsafe_data(pinfo->dummy_bytes, pinfo->dummy_len);
	printf("\nSaver model:\n");
	print_unsafe_data(pinfo->saver_model, 4);
	printf("\nReserved area data:\n");
	print_unsafe_data(pinfo->reserved_area, pinfo->reserved_area_len);
}

enum wu_error_type pi_dec(struct image_file *infile) {
	size_t filesize;
	unsigned char *filedata = read_file_to_mem(infile->name, &filesize);
	if (!filedata) {
		return wu_open_error;
	}

	enum wu_error_type err;
	struct pi_decompress_info pinfo;
	if (pi_check_sig(filedata)) {
		err = pi_parse_full_header(filedata,
			filesize, &pinfo);
	} else {
		printf("%s has no signature, but it might be an abbreviated "
			"file.", infile->name);
		return wu_invalid_sig;
		//err = pi_parse_abbreviated_header(filedata, filesize, &pinfo);
	}
	if (err) {
		return wu_unexpected_eof;
	}

	print_pi_info(&pinfo);

	alloc_sub_images(infile, 1);
	struct raw_img *restrict img = &infile->sub_img[0];
	img->w = 4;
	img->h = 4;
	img->bitdepth = 8;
	img->channels = 3;
	img->data = malloc(img->w * img->h * img->channels);
	memcpy(img->data, pinfo.palette, (size_t)img->channels * 16);
	img->id = strdup("palette");

	free(filedata);
	return wu_ok;
}

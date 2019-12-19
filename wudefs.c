#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "wudefs.h"

const char * wu_error_message(enum wu_error_type err) {
	switch (err) {
	case wu_ok:
		return "All OK :: Failed to test for error";
	case wu_alloc_error:
		return "Insufficient memory";
	case wu_unknown_file_type:
		return "Unsupported file format";
	case wu_invalid_params:
		return "FIXME! Invalid decoding parameters";
	case wu_open_error:
		return "Failed to open file for reading";
	case wu_unexpected_eof:
		return "Reached unexpected End Of File";
	case wu_invalid_sig:
		return "Corrupted or invalid file format signature";
	case wu_invalid_header:
		return "Corrupted or invalid format header";
	case wu_unsupported_feature:
		return "Unsupported feature in image";
	case wu_decoding_error:
		return "Failed to decode image";
	case wu_unknown_error:
		return "I knowingly returned an invalid code";
	default:
		return "???";
	}
}

void print_image_information(const struct image_file *file) {
	const struct raw_img *img = file->sub_img;

	printf("Contained images: %zu\n", file->nr);
	size_t overall_size = 0;
	for (size_t i = 0; i < file->nr; ++i) {
		if (file->is_animation) {
			printf(" */%zu, id: frame*\n", file->nr);
		} else {
			printf(" %zu/%zu", i+1, file->nr);
			if (img[i].id) {
				printf(", id: %s", img[i].id);
			}
			putchar('\n');
		}

		const size_t mem_size = img[i].w * img[i].h * img[i].channels
			* img[i].bitdepth / 8;
		printf("  dimensions: %ux%ux%hhux%hhu · %zu bytes\n",
			img[i].w, img[i].h, img[i].channels, img[i].bitdepth,
			mem_size);

		if (file->is_animation) {
			overall_size = mem_size * file->nr;
			break;
		} else {
			overall_size += mem_size;
		}
	}

	printf("Size in memory: %zu\n", overall_size);
}

void normalize_sub_images(struct image_file *file) {
	struct raw_img *img = file->sub_img;
	for (size_t i = 0; i < file->nr; ++i) {
		if (!img[i].true_channels) {
			img[i].true_channels = img[i].channels;
		}
		if (!img[i].true_bitdepth) {
			img[i].true_bitdepth = img[i].bitdepth;
		}
	}
}

void fit_sub_images(struct image_file *file, const size_t nr) {
	if (nr < file->nr) {
		struct raw_img *hold = realloc(file->sub_img,
			nr * sizeof(struct raw_img));
		if (hold) {
			file->sub_img = hold;
			file->nr = nr;
		} else {
			free(file->sub_img);
		}
		return;
	}
	puts("FIXME: fit_sub_images() called with a size greater than the "
		"one previously allocated.");
}

struct raw_img * alloc_sub_images(struct image_file *file, const size_t nr) {
	file->nr = nr;
	file->sub_img = calloc(nr, sizeof(struct raw_img));
	return file->sub_img;
}

void free_image_file(struct image_file *file) {
	for (size_t i = 0; i < file->nr; ++i) {
		free(file->sub_img[i].data);
		free(file->sub_img[i].id);
	}
	free(file->sub_img);
	free(file->err_msg);
	memset(file, 0, sizeof(struct image_file));
}

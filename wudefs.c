#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>
#include <errno.h>

#include "wudefs.h"

const char * wu_error_message(enum wu_error err) {
	switch (err) {
	case wu_ok:
		return "All OK :: Failed to test for error";
	case wu_alloc_error:
		return "Memory allocation error";
	case wu_unknown_file_type:
		return "Unsupported file format";
	case wu_invalid_params:
		return "Invalid decoding parameters";
	case wu_open_error:
		return "Failed to open file for reading";
	case wu_unexpected_eof:
		return "Reached unexpected End Of File";
	case wu_invalid_signature:
		return "Corrupted or invalid file format signature";
	case wu_invalid_header:
		return "Corrupted or invalid format header";
	case wu_unsupported_feature:
		return "Unsupported feature in image";
	case wu_decoding_error:
		return "Failed to decode image";
	case wu_exceeded_size_limit:
		return "The image exceeds either the max texture size or the "
			"configured size limit";
	case wu_unknown_error:
		return "An unknown and unexpected thing happened. Things are "
			"bad. Pray for my soul.";
	default:
		return "???";
	}
}

void print_image_information(const struct image_file *file) {
	const struct raw_img *img = file->sub_img;

	if (file->err_msg) {
		printf("Found warning: %s\n", file->err_msg);
	}

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

		size_t mem_size = img[i].w * img[i].h;
		printf("  dimensions: %zu x %zu x ", img[i].w, img[i].h);

		if (img[i].palette) {
			printf("1 (paletted) ");
		} else {
			mem_size *= img[i].channels;
			printf("%hhu ", img[i].channels);
			if (img[i].channels != img[i].true_channels) {
				printf("(%hhu) ", img[i].true_channels);
			}
		}

		mem_size = mem_size * img[i].bitdepth / 8;
		printf("x %hhu = %zu bytes\n", img[i].bitdepth, mem_size);

		if (file->is_animation) {
			overall_size = mem_size * file->nr;
			break;
		} else {
			overall_size += mem_size;
		}
	}

	if (file->nr > 1) {
		printf("Total size in memory: %zu\n", overall_size);
	}
}

void normalize_sub_images(struct image_file *file) {
	struct raw_img *img = file->sub_img;
	for (size_t i = 0; i < file->nr; ++i) {
		if (!img[i].true_channels) {
			img[i].true_channels = img[i].channels;
		}

		if (!img[i].layout) {
			switch (img[i].true_channels) {
			case 1:
				img[i].layout = gray;
				break;
			case 2:
				img[i].layout = gray_alpha;
				break;
			default:
				img[i].layout = rgba;
			}
		}

		if (!img[i].alignment) {
			img[i].alignment = 1;
		}

		if (!img[i].dec_scale) {
			img[i].dec_scale = 1.0f;
		}
	}
}

struct raw_img * realloc_sub_images(struct image_file *file, const size_t nr) {
	const size_t img_size = sizeof(*file->sub_img);
	struct raw_img *hold = realloc(file->sub_img, nr * img_size);
	if (hold) {
		const size_t old_size = file->nr;
		file->nr = nr;
		file->sub_img = hold;
		if (nr > old_size) {
			const size_t len = img_size * (nr - old_size);
			memset(file->sub_img + old_size, 0, len);
		}
	}
	return hold;
}

struct raw_img * alloc_sub_images(struct image_file *file, const size_t nr) {
	file->nr = nr;
	file->sub_img = calloc(nr, sizeof(*file->sub_img));
	return file->sub_img;
}

void free_image_file(struct image_file *file) {
	if (file->callback) {
		file->callback(file, NULL, NULL, 0);
	}

	for (size_t i = 0; i < file->nr; ++i) {
		free(file->sub_img[i].data);
		free(file->sub_img[i].palette);
		free(file->sub_img[i].id);
	}

	free(file->sub_img);
	free(file->err_msg);
	fclose(file->ifp);

	memset(file, 0, sizeof(struct image_file));
}

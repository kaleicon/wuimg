#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>
#include <errno.h>

#include "wudefs.h"
#include "wutree.h"
#include "common.h"

const char * wu_error_message(const enum wu_error err) {
	switch (err) {
	case wu_no_change:
		return "Nothing was done so nothing failed";
	case wu_ok:
		return "All OK";
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
	case wu_exceeds_size_limit:
		return "The image exceeds the max dimension limit";
	case wu_unknown_error:
		return "Purposely unspecified error o.O (unfinished code)";
	default:
		return "An unknown and unforeseen problem happened. Things are "
			"bad. Pray for my soul.";
	}
}

static size_t print_dimensions(const struct raw_img *img) {
	printf("  dimensions: %zu x %zu x %hhu", img->w, img->h, img->channels);

	size_t line;
	if (img->palette) {
		printf(" (paletted) x %hhu", img->bitdepth);
		line = img->w;
	} else {
		if (img->channels != img->true_channels) {
			printf(" (%hhu)", img->true_channels);
		}
		printf(" x %hhu", img->bitdepth);

		switch (img->bitdepth) {
		case rgb332:
			fputs(" (rgb332)", stdout);
			line = img->w;
			break;
		case argb1555:
			fputs(" (argb1555)", stdout);
			line = img->w * 2;
			break;
		default:
			line = img->w * img->channels;
		}
		if (img->attr) {
			const char *attr[] = {"float", "inverted", "planar"};
			for (size_t i = 0; i < ARRAY_LEN(attr); ++i) {
				if ((img->attr >> i) & 1) {
					printf(" (%s)", attr[i]);
				}
			}
		}
	}

	const size_t mem_size = scanline_length(line, img->bitdepth,
		img->alignment) * img->h;
	printf(" = %zu bytes", mem_size);
	if (img->dec_scale != 1) {
		printf(" @ %.2fx original", img->dec_scale);
	}
	putchar('\n');
	return mem_size;
}

void print_image_information(const struct image_file *file, const int verbosity) {
	const struct raw_img *img = file->sub_img;

	size_t max_x = 80;
	size_t max_y = 20;
	switch (verbosity) {
	case 1: max_x = 320; max_y = 80; break;
	case 2: max_x = SIZE_MAX; max_y = SIZE_MAX; break;
	}

	tree_print(&file->metadata, max_x, max_y);

	if (file->err_msg) {
		printf("Found warning: %s\n", file->err_msg);
	}
	printf("Contained sub-images: %zu\n", file->nr);
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

		const size_t mem_size = print_dimensions(img + i);

		if (file->is_animation) {
			overall_size = mem_size * file->nr;
			break;
		} else {
			overall_size += mem_size;
		}
	}

	if (file->nr > 1 || file->is_animation) {
		printf("Total size in memory: %zu\n", overall_size);
	}
}

static unsigned char compact_alignment(struct raw_img *img,
const unsigned char to) {
	const size_t line = img->w * (img->palette ? 1 : img->channels);
	const size_t src = scanline_length(line, img->bitdepth, img->alignment);
	const size_t dst = scanline_length(line, img->bitdepth, to);
	if (src != dst) {
		for (size_t i = 0; i < img->h; ++i) {
			memmove(img->data + dst*i, img->data + src*i, dst);
		}
	}
	return to;
}

void normalize_sub_images(struct image_file *file) {
	struct raw_img *img = file->sub_img;
	for (size_t i = 0; i < file->nr; ++i) {
		if (img[i].palette) {
			img[i].channels = 1;
		}

		if (!img[i].true_channels) {
			img[i].true_channels = img[i].channels;
		}

		if (!img[i].layout) {
			if (img[i].palette) {
				img[i].layout = rgba;
			} else {
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
		}

		if (!img[i].alignment) {
			img[i].alignment = 1;
		} else if (img[i].alignment > 8 && img[i].bitdepth >= 8) {
			img[i].alignment = compact_alignment(img + i, 8);
		}

		if (!img[i].dec_scale) {
			img[i].dec_scale = 1;
		}
	}
}

static void raw_img_free(struct raw_img *img) {
	free(img->data);
	free(img->palette);
	free(img->id);
}

static void raw_img_free_range(struct raw_img *img, const size_t start,
const size_t end) {
	for (size_t i = start; i < end; ++i) {
		raw_img_free(img + i);
	}
}

size_t raw_img_addbuf(struct raw_img *img) {
	if (!img->alignment) {
		img->alignment = 1;
	}
	const size_t len = scanline_length(img->w * img->channels, img->bitdepth,
		img->alignment) * img->h;
	img->data = malloc(len);
	if (img->data) {
		return len;
	}
	return 0;
}

void raw_img_clear(struct raw_img *img) {
	raw_img_free(img);
	memset(img, 0, sizeof(*img));
}

struct raw_img * realloc_sub_images(struct image_file *file, const size_t nr) {
	if (nr < file->nr) {
		raw_img_free_range(file->sub_img, nr, file->nr);
	}

	const size_t img_size = sizeof(*file->sub_img);
	struct raw_img *hold = realloc(file->sub_img, nr * img_size);
	if (hold) {
		if (nr > file->nr) {
			const size_t len = img_size * (nr - file->nr);
			memset(hold + file->nr, 0, len);
		}
		file->nr = nr;
		file->sub_img = hold;
	}
	return hold;
}

struct raw_img * alloc_sub_images(struct image_file *file, const size_t nr) {
	file->sub_img = calloc(nr, sizeof(*file->sub_img));
	if (file->sub_img) {
		file->nr = nr;
	}
	return file->sub_img;
}

void free_image_file(struct image_file *file) {
	raw_img_free_range(file->sub_img, 0, file->nr);
	free(file->sub_img);
	free(file->err_msg);
	tree_unroot(&file->metadata);
	if (file->ifp) {
		fclose(file->ifp);
	}
}

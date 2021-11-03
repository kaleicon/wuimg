#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>
#include <errno.h>

#include "wudefs.h"
#include "wutree.h"
#include "common.h"
#include "raster/raster.h"

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
		return "Image exceeds the max dimension limit";
	case wu_unknown_error:
		return "Purposely unspecified error o.O";
	}
	return "An unknown and unforeseen problem occurred. Things are bad. "
		"Pray for my soul.";
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

void raw_img_yuva_info(const struct raw_img *img, struct yuva_info *info) {
	const size_t align = img->alignment ? img->alignment : 1;

	info->ya.w = img->w;
	info->ya.h = img->h;
	info->ya.stride = scanline_length(img->w, img->bitdepth, align);
	info->ya.size = info->ya.stride * info->ya.h;

	const unsigned horz = (img->subsamp >> 2) + 1;
	const unsigned vert = (img->subsamp & 0x3) + 1;

	info->uv.w = (horz == 1) ? img->w : (img->w+1) / horz;
	info->uv.h = (vert == 1) ? img->h : (img->h+1) / vert;
	info->uv.stride = scanline_length(info->uv.w, img->bitdepth, align);
	info->uv.size = info->uv.stride * info->uv.h;

	// [0] = Y, [1] = U, [2] = V, [3] = Alpha
	info->yuva[0] = img->data;
	info->yuva[1] = info->yuva[0] + info->ya.size;
	info->yuva[2] = info->yuva[1] + info->uv.size;
	info->yuva[3] = (img->channels > 3)
		? info->yuva[2] + info->uv.size
		: NULL;
}

size_t raw_img_stride(const struct raw_img *img) {
	uint8_t align = img->alignment;
	if (!align) {
		align = 1;
	}
	return scanline_length(img->w * img->channels, img->bitdepth, align);
}

size_t raw_img_size(const struct raw_img *img) {
	return raw_img_stride(img) * img->h;
}

size_t raw_img_addbuf(struct raw_img *img) {
	const size_t stride = raw_img_stride(img);
	img->data = malloc(stride * img->h);
	if (img->data) {
		return stride;
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

	const size_t struct_size = sizeof(*file->sub_img);
	struct raw_img *hold = realloc(file->sub_img, nr * struct_size);
	if (hold) {
		if (nr > file->nr) {
			const size_t len = struct_size * (nr - file->nr);
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

static size_t print_dimensions(const struct raw_img *img) {
	printf("  dimensions: %zu x %zu x %d",
		img->w, img->h, img->channels);

	size_t mem_size;
	if (img->palette) {
		printf(" (paletted) x %d", img->bitdepth);
		mem_size = scanline_length(img->w, img->bitdepth,
			img->alignment) * img->h;
	} else if (img->yuva) {
		const char *type = (img->channels == 4) ? "yuva" : "yuv";
		const int h_samp = (img->subsamp >> 2) + 1;
		const int v_samp = (img->subsamp & 0x03) + 1;
		printf(" (%s %d%d) x %d", type, h_samp, v_samp, img->bitdepth);

		struct yuva_info info;
		raw_img_yuva_info(img, &info);
		mem_size = info.ya.size + info.uv.size * 2;
		if (img->channels == 4) {
			mem_size += info.ya.size;
		}
	} else {
		printf(" x %d", img->bitdepth);
		if (img->attr) {
			const char *attr[] = {"inverted", "float", "332", "1555"};
			printf(" (%s)", attr[img->attr - 1]);
		}
		mem_size = scanline_length(img->w * img->channels,
			img->bitdepth, img->alignment) * img->h;
	}

	printf(" x %d = %zu bytes", img->alignment, mem_size);
	if (img->dec_scale != 1) {
		printf(", %.2fx original", img->dec_scale);
	}
	putchar('\n');
	return mem_size;
}

void image_file_print(const struct image_file *file, const int verbosity) {
	size_t max_x = 0;
	size_t max_y = 0;
	switch (verbosity) {
	case 0: max_x = 80; max_y = 20; break;
	case 1: max_x = 320; max_y = 80; break;
	case 2: max_x = SIZE_MAX; max_y = SIZE_MAX; break;
	}

	tree_print(&file->metadata, max_x, max_y);

	if (file->errors.str) {
		printf("Found warning: %s\n", file->errors.str);
	}
	printf("Contained sub-images: %zu\n", file->nr);

	const struct raw_img *img = file->sub_img;
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

		if (img[i].data) {
			const size_t mem_size = print_dimensions(img + i);
			if (file->is_animation) {
				overall_size = mem_size * file->nr;
				break;
			} else {
				overall_size += mem_size;
			}
		} else {
			puts("  Not loaded");
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

void image_file_normalize(struct image_file *file) {
	struct raw_img *img = file->sub_img;
	for (size_t i = 0; i < file->nr; ++i) {
		if (!img[i].data) {
			continue;
		}
		const char *err_msg = raster_geom_verify(img[i].palette,
			img[i].channels, img[i].bitdepth, img[i].attr);
		if (err_msg) {
			fatal_bug("Bad image", err_msg);
		}

		switch (img[i].attr) {
		case pix_packing_332:
			img[i].channels = 1;
			img[i].bitdepth = 8;
			break;
		case pix_packing_1555:
			img[i].channels = 1;
			img[i].bitdepth = 16;
			break;
		default:
			break;
		}

		if (!img[i].layout) {
			if (img[i].palette || img[i].attr == pix_packing_332) {
				img[i].layout = pix_rgba;
			} else {
				if (img[i].channels >= 3) {
					img[i].layout = pix_rgba;
				} else {
					img[i].layout = pix_gray;
				}
			}
		}

		if (!img[i].disable_alpha && !img[i].palette) {
			const uint8_t a = pix_layout_offset(img[i].layout, pix_alpha);
			if (a >= img[i].channels) {
				img[i].disable_alpha = true;
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

enum wu_error image_file_total_decoded(struct image_file *file, const size_t o) {
	if (o < file->nr) {
		if (!o) {
			return wu_decoding_error;
		}
		realloc_sub_images(file, o);
	}
	return wu_ok;
}

void image_file_error_append(struct image_file *file, const char *str) {
	wustr_append(&file->errors, str);
}

void image_file_free(struct image_file *file) {
	raw_img_free_range(file->sub_img, 0, file->nr);
	free(file->sub_img);
	wustr_free(&file->errors);
	tree_unroot(&file->metadata);
	if (file->ifp) {
		fclose(file->ifp);
	}
	memset(file, 0, sizeof(*file));
}

static size_t fit(const size_t w, const size_t h, const size_t dw,
const size_t dh, const size_t m) {
	return zumax(1, zumin(m, zumax(w / dw, h / dh)));
}

size_t image_fit_factor(const struct wu_conf *conf, const size_t w,
const size_t h, size_t max, const bool partial_decode) {
	if (!max) {
		max = SIZE_MAX;
	}
	if (partial_decode) {
		return fit(w, h, conf->fb.w, conf->fb.h, max);
	}
	const size_t m = conf->max_img_size;
	return fit(w, h, m, m, max);
}

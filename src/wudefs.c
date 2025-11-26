// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2019 kaleido
#include <stdlib.h>
#include <string.h>

#include "misc/common.h"
#include "misc/file.h"
#include "misc/math.h"
#include "wudefs.h"

static void wuimg_free_range(struct wuimg *img, const size_t start,
const size_t end) {
	for (size_t i = start; i < end; ++i) {
		wuimg_free(img + i);
	}
}

struct wuimg * realloc_sub_images(struct image_file *file, const size_t nr) {
	const size_t min = zumin(nr, file->nr);
	wuimg_free_range(file->sub_img, min, file->nr);
	struct wuimg *hold = small_realloc(file->sub_img, nr, sizeof(*hold));
	if (hold) {
		memset(hold + file->nr, 0, (nr - min) * sizeof(*hold));
		file->nr = nr;
		file->sub_img = hold;
	} else {
		file->nr = min;
	}
	return hold;
}

struct wuimg * alloc_sub_images(struct image_file *file, const size_t nr) {
	file->sub_img = small_calloc(nr, sizeof(*file->sub_img));
	file->nr = (file->sub_img) ? nr : 0;
	return file->sub_img;
}

void image_file_free_if_single(struct image_file *file) {
	if (!file->dec_state && file->nr == 1) {
		struct wuimg *img = file->sub_img;
		if (!img->borrowed && !img->frames) {
			free(img->data);
			img->data = NULL;
		}
	}
}

void image_file_print(const struct image_file *file, FILE *out,
const int verbosity, const bool unloaded_too) {
	size_t max_x = 0;
	size_t max_y = 0;
	switch (verbosity) {
	case 1: max_x = 80; max_y = 16; break;
	case 2: max_x = 320; max_y = 80; break;
	default:
		if (verbosity <= 0) {
			return;
		}
		max_x = SIZE_MAX; max_y = SIZE_MAX;
	}

	tree_print(&file->metadata, max_x, max_y, 0, out);

	if (file->errors.str) {
		fputs("Library warning: ", out);
		if (file->errors.len > max_x) {
			fputs("<omitted long message>\n", out);
		} else {
			wustr_print(&file->errors, out);
		}
	}
	fprintf(out, "Contained sub-images: %zu\n", file->nr);

	size_t overall_size = 0;
	size_t not_loaded = 0;
	for (size_t i = 0; i < file->nr; ++i) {
		const struct wuimg *img = file->sub_img + i;
		if (unloaded_too || wuimg_has_data(img)) {
			fprintf(out, " %zu/%zu: ", i+1, file->nr);
			overall_size += wuimg_print(img, out, verbosity);
		} else {
			++not_loaded;
		}
	}
	if (not_loaded) {
		fprintf(out, "Sub-images not loaded: %zu\n", not_loaded);
	}

	if (file->nr > 1) {
		fprintf(out, "Total size in memory: %zu\n", overall_size);
	}
}

size_t image_file_size(const struct image_file *file) {
	if (file->map.ptr) {
		return file->map.len;
	}
	FILE *ifp = file->ifp;
	const long cur = ftell(ifp);
	fseek(ifp, 0, SEEK_END);
	const long size = ftell(ifp);
	fseek(ifp, cur, SEEK_SET);
	return (size_t)lmax(0, size);
}

enum wu_error image_file_total_decoded(struct image_file *file, const size_t o) {
	if (!o) {
		return wu_decoding_error;
	} else if (o > file->nr) {
		fatal_bug("Bad image", "Decoded more images than allocated?");
	} else if (o < file->nr) {
		realloc_sub_images(file, o);
	}
	return wu_ok;
}

void image_file_error_print(const struct image_file *file, enum wu_error err,
FILE *out) {
	fputs(wu_error_str(err), out);
	if (file->errors.str) {
		fputs(" (\"", out);
		wuptr_print(wuptr_trim_end(wuptr_wustr(file->errors), '\n'), out);
		fputs("\")", out);
	}
	fputc('\n', out);
}

void image_file_strerror_append(struct image_file *file, const char *str) {
	wustr_append_line(&file->errors, str, true);
}

void image_file_error_append(struct image_file *file, const enum wu_error st) {
	image_file_strerror_append(file, wu_error_str(st));
}

void image_file_free(struct image_file *file) {
	wuimg_free_range(file->sub_img, 0, file->nr);
	free(file->sub_img);
	wustr_free(&file->errors);
	tree_unroot(&file->metadata);
	if (file->map.ptr && !file->keep_map) {
		file_unmap(&file->map);
	}
	if (file->ifp && !file->keep_file) {
		fclose(file->ifp);
	}
}

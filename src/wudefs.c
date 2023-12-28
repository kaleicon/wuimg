// SPDX-License-Identifier: 0BSD
#include <stdlib.h>
#include <string.h>

#include "misc/common.h"
#include "misc/math.h"
#include "wudefs.h"

void image_file_free_end(struct image_file *infile) {
	free(infile->dec_state);
}

static void wuimg_free_range(struct wuimg *img, const size_t start,
const size_t end) {
	for (size_t i = start; i < end; ++i) {
		wuimg_free(img + i);
	}
}

struct wuimg * realloc_sub_images(struct image_file *file, const size_t nr) {
	if (nr < 1) {
		return NULL;
	}
	if (nr < file->nr) {
		wuimg_free_range(file->sub_img, nr, file->nr);
	}

	const size_t struct_size = sizeof(*file->sub_img);
	struct wuimg *hold = realloc(file->sub_img, nr * struct_size);
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

struct wuimg * alloc_sub_images(struct image_file *file, const size_t nr) {
	if (nr < 1) {
		return NULL;
	}
	file->sub_img = calloc(nr, sizeof(*file->sub_img));
	if (file->sub_img) {
		file->nr = nr;
	}
	return file->sub_img;
}

void image_file_free_if_single(struct image_file *file) {
	if (!file->dec_state && file->nr == 1) {
		struct wuimg *img = file->sub_img;
		if (!img->borrowed) {
			free(img->data);
			img->data = NULL;
		}
	}
}

void image_file_print(const struct image_file *file, const int verbosity) {
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

	tree_print(&file->metadata, max_x, max_y, 0, stdout);

	if (file->errors.str) {
		fputs("Found warning: ", stdout);
		wustr_print(&file->errors, stdout);
	}
	printf("Contained sub-images: %zu\n", file->nr);

	size_t overall_size = 0;
	for (size_t i = 0; i < file->nr; ++i) {
		const struct wuimg *img = file->sub_img + i;
		printf(" %zu/%zu: ", i+1, file->nr);
		overall_size += wuimg_print(img, verbosity);
	}

	if (file->nr > 1) {
		printf("Total size in memory: %zu\n", overall_size);
	}
}

void image_file_normalize(struct image_file *file) {
	if (!file->nr) {
		fatal_bug("Bad image", "No sub-images contained!");
	}
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

void image_file_strerror_append(struct image_file *file, const char *str) {
	wustr_append_line(&file->errors, str, true);
}

void image_file_error_append(struct image_file *file, const enum wu_error st) {
	image_file_strerror_append(file, wu_error_message(st));
}

void image_file_free(struct image_file *file) {
	wuimg_free_range(file->sub_img, 0, file->nr);
	free(file->sub_img);
	wustr_free(&file->errors);
	tree_unroot(&file->metadata);
	if (file->map.data && !file->keep_map) {
		file_unmap(&file->map);
	}
	if (file->ifp && !file->keep_file) {
		fclose(file->ifp);
	}
}


struct wuimg * image_cur_sub_img(const struct image_context *image) {
	return image->file.sub_img + image->state.idx;
}

bool image_cur_is_anim(const struct image_context *image) {
	const struct wuimg *img = image_cur_sub_img(image);
	return img->evolving || img->frames;
}

enum image_event image_cur_events(const struct image_context *image) {
	const struct wuimg *img = image_cur_sub_img(image);
	return ev_subcycle
		| (img->evolving ? ev_time : 0)
		| (img->frames ? ev_frame : 0)
		| (img->scalable ? ev_scale : 0);
}

enum image_event image_zoom(struct image_context *image, float new_zoom) {
	const float max = 1 << WU_SCALING_POW;
	const float min = 1.0f/max;

	enum image_event ev = 0;
	new_zoom = fclampf(new_zoom, min, max);
	if (new_zoom != image->state.zoom) {
		ev = (new_zoom > image->state.zoom) ? ev_upscale : ev_downscale;
		image->state.zoom = new_zoom;
	}
	return ev;
}

enum image_event image_sub_cycle(struct image_context *image, int steps) {
	const int c = imod(image->state.idx + steps, (int)image->file.nr);
	if (c != image->state.idx) {
		image->state.idx = c;
		return ev_subcycle;
	}
	return 0;
}

enum image_event image_frame_cycle(struct image_context *image, int steps) {
	const struct image_frames *frames = image_cur_sub_img(image)->frames;
	if (frames) {
		const int f = imod(image->state.frame + steps, (int)frames->nr);
		if (f != image->state.frame) {
			image->state.frame = f;
			return ev_frame;
		}
	}
	return 0;
}

void image_reset(struct image_context *image) {
	image->name = NULL;
	image->file = (struct image_file){0};
	image->state.idx = 0;
	image->state.frame = 0;
	image->state.time = 0;
	image->fn = NULL;
}

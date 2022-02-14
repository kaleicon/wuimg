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
	case wu_display_error:
		return "Error ocurred during display";
	case wu_unknown_error:
		return "Purposely unspecified error o.O";
	}
	return "An unknown and unforeseen problem occurred. Things are bad. "
		"Pray for my soul.";
}

static void raw_img_free(struct raw_img *img) {
	free(img->data);
	free(img->u.palette);
	free(img->frames);
	free(img->id);
}

static void raw_img_free_range(struct raw_img *img, const size_t start,
const size_t end) {
	for (size_t i = start; i < end; ++i) {
		raw_img_free(img + i);
	}
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

size_t raw_img_nr_frames(const struct raw_img *img) {
	if (img->frames) {
		return img->frames->nr;
	}
	return 1;
}

struct image_frames * raw_img_alloc_frames(struct raw_img *img, size_t nr) {
	img->frames = malloc(sizeof(*img->frames) + nr * sizeof(*img->frames->f));
	if (img->frames) {
		img->frames->nr = nr;
	}
	return img->frames;
}

static size_t subsamp_dim(const size_t dim, const uint8_t subsamp) {
	if (subsamp > 1) {
		return (dim + 1) / subsamp;
	}
	return dim;
}

static size_t plane_calc_size(const struct raw_img *img,
struct plane_info *plane) {
	plane->w = subsamp_dim(img->w, plane->x.subsamp);
	plane->h = subsamp_dim(img->h, plane->y.subsamp);
	plane->stride = scanline_length(plane->w, img->bitdepth, img->alignment);
	plane->size = scanline_length(plane->h, 8, img->u.planes->v_pad)
		* plane->stride;
	return plane->size;
}

void raw_img_plane_resolve(struct raw_img *img) {
	size_t total = 0;
	for (int i = 0; i < img->channels; ++i) {
		struct plane_info *p = img->u.planes->p + i;
		p->ptr = img->data + total;
		total += plane_calc_size(img, p);
	}
}

bool raw_img_plane_alloc(struct raw_img *img) {
	size_t offsets[4];
	size_t total = 0;
	struct plane_info *p = img->u.planes->p;
	for (int i = 0; i < img->channels; ++i) {
		offsets[i] = total; // Start with 0
		total += plane_calc_size(img, p + i);
	}

	img->data = malloc(total);
	if (img->data) {
		for (int i = 0; i < img->channels; ++i) {
			p[i].ptr = img->data + offsets[i];
		}
	}
	return (bool)img->data;
}

void raw_img_plane_subsamp(struct raw_img *img, const enum pix_subsampling s) {
	const uint8_t horz = (uint8_t)((s >> 2) + 1);
	const uint8_t vert = (uint8_t)((s & 0x3) + 1);

	struct plane_info *p = img->u.planes->p;
	p[1].x.subsamp = horz;
	p[1].y.subsamp = vert;
	p[2].x.subsamp = horz;
	p[2].y.subsamp = vert;
}

static void set_img_mode(struct raw_img *img, const enum image_mode mode) {
	if (img->mode) {
		fatal_bug("Bad image mode", "Image mode had been set previously");
	}
	img->mode = mode;
}

struct image_planes * raw_img_plane_init(struct raw_img *img) {
	img->u.planes = calloc(sizeof(*img->u.planes), 1);
	if (img->u.planes) {
		set_img_mode(img, image_mode_planar);
		if (!img->alignment) {
			img->alignment = 1;
		}
		img->u.planes->v_pad = 1;
	}
	return img->u.planes;
}

bool raw_img_plane_from_params(struct raw_img *img) {
	if (raw_img_plane_init(img)) {
		return raw_img_plane_alloc(img);
	}
	return false;
}

struct raster_pal * raw_img_set_palette(struct raw_img *img,
struct raster_pal *pal) {
	if (pal) {
		set_img_mode(img, image_mode_palette);
		img->u.palette = pal;
	}
	return pal;
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


void image_file_free_if_single(struct image_file *file) {
	if (!file->events && !file->dec_state && file->nr == 1) {
		struct raw_img *img = file->sub_img;
		free(img->data);
		img->data = NULL;
	}
}

static size_t print_dimensions(const struct raw_img *img) {
	printf("  dimensions: %zu x %zu x %d", img->w, img->h, img->channels);

	size_t memsize = 0;
	if (img->mode == image_mode_planar) {
		struct image_planes *planes = img->u.planes;
		printf(" (planar, %s)", color_space_str(planes->cs));
		for (int i = 0; i < img->channels; ++i) {
			memsize += planes->p[i].size;
		}
	} else {
		if (img->mode == image_mode_palette) {
			fputs(" (paletted)", stdout);
		}
		memsize = scanline_length(img->w * img->channels, img->bitdepth,
			img->alignment) * img->h;
	}

	printf(" x %d", img->bitdepth);
	if (img->attr) {
		const char *attr[] = {"inverted", "float", "332", "1555"};
		printf(" (%s)", attr[img->attr - 1]);
	}

	printf(" = %zu bytes", memsize);
	if (img->dec_scale != 1) {
		printf(", %.2fx original", img->dec_scale);
	}
	putchar('\n');
	return memsize;
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
		fputs("Found warning: ", stdout);
		fwrite(file->errors.str, 1, file->errors.len, stdout);
	}
	printf("Contained sub-images: %zu\n", file->nr);

	size_t overall_size = 0;
	for (size_t i = 0; i < file->nr; ++i) {
		const struct raw_img *img = file->sub_img + i;
		printf(" %zu/%zu", i+1, file->nr);
		if (img->frames) {
			printf(", frames: %zu", img->frames->nr);
		}
		if (img->id) {
			printf(", id: %s", img->id);
		}
		putchar('\n');

		if (img->data) {
			overall_size += print_dimensions(img);
		} else {
			puts("  Not loaded");
		}
	}

	if (file->nr > 1) {
		printf("Total size in memory: %zu\n", overall_size);
	}
}

static void try_better_alignment(struct raw_img *img) {
	const size_t bytes = (img->w * img->channels * img->bitdepth + 7) / 8;
	const size_t align = img->alignment - 1;
	const size_t diff = ((bytes + align) & (~align)) - bytes;
	if (diff < 8) {
		img->alignment = 8;
	}
}

void image_file_normalize(struct image_file *file) {
	if (!file->nr) {
		fatal_bug("Bad image", "No sub-images contained!");
	}
	for (size_t i = 0; i < file->nr; ++i) {
		struct raw_img *img = file->sub_img + i;
		if (!img->data) {
			continue;
		}

		const char *err_msg = raster_geom_verify(
			img->mode == image_mode_palette, img->channels,
			img->bitdepth, img->attr);
		if (err_msg) {
			fatal_bug("Bad image", err_msg);
		}

		switch (img->attr) {
		case pix_packing_332:
			img->channels = 1;
			img->bitdepth = 8;
			break;
		case pix_packing_1555:
			img->channels = 1;
			img->bitdepth = 16;
			break;
		default:
			break;
		}

		if (!img->alignment) {
			img->alignment = 1;
		} else if (img->mode != image_mode_planar && img->alignment > 8) {
			try_better_alignment(img);
		}

		if (!img->layout) {
			if (img->mode == image_mode_palette
			|| img->attr == pix_packing_332) {
				img->layout = pix_rgba;
			} else {
				if (img->channels >= 3) {
					img->layout = pix_rgba;
				} else {
					img->layout = pix_gray;
				}
			}
		}

		if (!img->disable_alpha && img->mode != image_mode_palette) {
			const uint8_t a = pix_layout_offset(img->layout, pix_alpha);
			if (a >= img->channels) {
				img->disable_alpha = true;
			}
		}

		if (!img->dec_scale) {
			img->dec_scale = 1;
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
	wustr_append_line(&file->errors, str);
}

void image_file_free(struct image_file *file) {
	raw_img_free_range(file->sub_img, 0, file->nr);
	free(file->sub_img);
	wustr_free(&file->errors);
	tree_unroot(&file->metadata);
	if (file->ifp) {
		fclose(file->ifp);
	}
}


struct raw_img * image_cur_sub_img(const struct image_context *image) {
	return image->file.sub_img + image->state.idx;
}

enum image_event image_zoom(struct image_context *image, float new_zoom) {
	enum image_event ev = 0;
	if (new_zoom != image->state.zoom) {
		ev = (new_zoom > image->state.zoom) ? ev_upscale : ev_downscale;
		image->state.zoom = new_zoom;
	}
	return ev;
}

enum image_event image_sub_cycle(struct image_context *image, int subcycle) {
	const int c = imod(image->state.idx + subcycle, (int)image->file.nr);
	if (c != image->state.idx) {
		image->state.idx = c;
		return ev_subcycle;
	}
	return 0;
}

enum image_event image_frame_cycle(struct image_context *image, int steps) {
	const struct image_frames *frames =
		image->file.sub_img[image->state.idx].frames;
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
	image->file = (struct image_file){0};
	image->state.idx = 0;
	image->state.frame = 0;
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

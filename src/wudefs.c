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
	case wu_open_error:
		return "Failed to open file for reading";
	case wu_unknown_file_type:
		return "Unknown file format";
	case wu_unexpected_eof:
		return "Unexpected End Of File";
	case wu_invalid_signature:
		return "Corrupted or invalid file format signature";
	case wu_invalid_header:
		return "Corrupted or invalid format header";
	case wu_invalid_params:
		return "Invalid decoding parameters. This is probably a bug!";
	case wu_unsupported_feature:
		return "Unsupported feature in image";
	case wu_no_image_data:
		return "Header-only file with no image data";
	case wu_exceeds_size_limit:
		return "Image exceeds the configured or display dimension limit";
	case wu_int_overflow:
		return "Integer overflow";
	case wu_decoding_error:
		return "Failed to decode image";
	case wu_display_error:
		return "Error ocurred during display";
	case wu_unknown_error:
		return "Purposely unspecified error o.O";
	}
	return "An unknown and unforeseen problem occurred. Things are bad. "
		"Pray for my soul.";
}

void raw_img_exif_orientation(struct raw_img *img, const int orientation) {
	int m;
	int r;
	switch (orientation) {
	case 1: m = 0; r = 0; break;
	case 2: m = 1; r = 2; break;
	case 3: m = 0; r = 2; break;
	case 4: m = 1; r = 0; break;
	case 5: m = 1; r = 3; break;
	case 6: m = 0; r = 1; break;
	case 7: m = 1; r = 1; break;
	case 8: m = 0; r = 3; break;
	default: return;
	}
	img->mirror = img->mirror ^ m;
	img->rotate = (img->rotate + r) & 0x03;
}

static void find_better_alignment(struct raw_img *img) {
	if (img->mode == image_mode_planar) {
		return;
	}
	const uint8_t ch = img->channels;
	const size_t bytes = (img->w * ch * img->bitdepth + 7) / 8;
	const size_t align = img->alignment - 1;
	const size_t diff = ((bytes + align) & (~align)) - bytes;
	if (diff < 8) {
		img->alignment = 8;
	}
}

static size_t subsamp_dim(const size_t dim, struct plane_dim *s) {
	if (s->subsamp > 1) {
		return (dim + 1) / s->subsamp;
	}
	s->subsamp = 1;
	return dim;
}

static size_t plane_calc_size(struct raw_img *img, const size_t i) {
	struct plane_info *p = img->u.planes->p + i;
	p->w = subsamp_dim(img->w, &p->x);
	p->h = subsamp_dim(img->h, &p->y);
	if (p->w < 1 || p->h < 1) {
		return 0;
	}
	p->stride = scanline_length(p->w, img->bitdepth, img->alignment);
	p->size = scanline_length(p->h, 8, img->u.planes->v_pad) * p->stride;
	return p->size;
}

static bool test_overflow(struct raw_img *img) {
	const uint8_t ch = (img->mode == image_mode_planar) ? 1 : img->channels;
	const bool ok = raster_test_overflow(img->w, img->h, ch, img->bitdepth,
		img->alignment);
	if (ok && img->mode == image_mode_planar) {
		size_t limit = SIZE_MAX;
		for (uint8_t i = 0; i < img->channels; ++i) {
			const size_t size = plane_calc_size(img, i);
			if (size > limit) {
				return false;
			}
			limit -= size;
		}
	}
	return ok;
}

enum wu_error raw_img_verify(struct raw_img *img) {
	const char *err_msg = raster_geom_verify(img->channels, img->bitdepth,
		img->attr, img->mode == image_mode_palette);
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
	}

	if (!test_overflow(img)) {
		return wu_int_overflow;
	}

	if (img->alignment > 8) {
		find_better_alignment(img);
	}

	if (!img->layout) {
		if (img->mode == image_mode_palette || img->channels >= 3
		|| img->attr == pix_packing_332) {
			img->layout = pix_rgba;
		} else {
			img->layout = pix_gray;
		}
	}
	if (img->mode != image_mode_palette) {
		const uint8_t a = pix_layout_offset(img->layout, pix_alpha);
		if (a >= img->channels) {
			img->alpha = alpha_ignore;
		}
	}
	if (!img->dec_scale) {
		img->dec_scale = 1;
	}
	return wu_ok;
}

bool raw_img_exceeds_limit(const struct raw_img *img,
const struct wu_conf *wuconf) {
	return zumax(img->w, img->h) > wuconf->max_img_size;
}

size_t raw_img_stride(const struct raw_img *img) {
	return scanline_length(img->w * img->channels, img->bitdepth,
		img->alignment);
}

size_t raw_img_size(const struct raw_img *img) {
	return raw_img_stride(img) * img->h;
}

size_t raw_img_plane_resolve(struct raw_img *img) {
	size_t total = 0;
	for (uint8_t i = 0; i < img->channels; ++i) {
		total += plane_calc_size(img, i);
	}
	return total;
}

static void plane_alloc(struct raw_img *img) {
	const size_t total = raw_img_plane_resolve(img);
	img->data = malloc(total);
	if (img->data) {
		struct plane_info *p = img->u.planes->p;
		size_t pos = 0;
		for (uint8_t i = 0; i < img->channels; ++i) {
			p[i].ptr = img->data + pos;
			pos += p[i].size;
		}
	}
}

bool raw_img_alloc_noverify(struct raw_img *img) {
	if (img->mode == image_mode_planar) {
		plane_alloc(img);
	} else {
		img->data = malloc(raw_img_size(img));
	}
	return img->data;
}

enum wu_error raw_img_alloc(struct raw_img *img) {
	const enum wu_error st = raw_img_verify(img);
	if (st == wu_ok) {
		return raw_img_alloc_noverify(img) ? wu_ok : wu_alloc_error;
	}
	return st;
}

static void set_img_mode(struct raw_img *img, const enum image_mode mode) {
	if (img->mode != image_mode_raw) {
		fatal_bug("Bad image mode", "Image mode had been set previously");
	}
	img->mode = mode;
}

void raw_img_plane_subsamp(struct raw_img *img, const uint8_t horz,
const uint8_t vert) {
	struct plane_info *p = img->u.planes->p;
	if (img->channels >= 2) {
		if (img->channels >= 3) {
			p[2].x.subsamp = horz;
			p[2].y.subsamp = vert;
		}
		p[1].x.subsamp = horz;
		p[1].y.subsamp = vert;
	}
}

struct image_planes * raw_img_plane_init(struct raw_img *img) {
	struct image_planes *planes = calloc(sizeof(*planes)
		+ img->channels * sizeof(*planes->p), 1);
	if (planes) {
		img->u.planes = planes;
		set_img_mode(img, image_mode_planar);
		if (!img->alignment) {
			img->alignment = 1;
		}
		planes->v_pad = 1;
	}
	return planes;
}

struct raster_pal * raw_img_set_palette(struct raw_img *img,
struct raster_pal *pal) {
	if (pal) {
		set_img_mode(img, image_mode_palette);
		img->u.palette = pal;
	}
	return pal;
}

size_t raw_img_frames_nr(const struct raw_img *img) {
	return img->frames ? img->frames->nr : 1;
}

struct image_frames * raw_img_frames_init(struct raw_img *img, size_t nr) {
	img->frames = calloc(sizeof(*img->frames) + nr * sizeof(*img->frames->f), 1);
	if (img->frames) {
		img->frames->nr = nr;
	}
	return img->frames;
}

bool raw_img_clone(struct raw_img *dst, struct raw_img *src) {
	*dst = *src;
	dst->data = NULL;
	dst->frames = NULL;
	dst->mode = image_mode_raw;
	dst->cs = color_space_ref(&src->cs);
	switch (src->mode) {
	case image_mode_raw: break;
	case image_mode_palette:
		return raw_img_set_palette(dst,
			memdup(src->u.palette, sizeof(*src->u.palette)));
	case image_mode_planar:
		return raw_img_plane_init(dst);
	}
	return true;
}

static void raw_img_free(struct raw_img *img) {
	free(img->data);
	switch (img->mode) {
	case image_mode_planar:
		free(img->u.planes);
		break;
	case image_mode_palette:
		free(img->u.palette);
		break;
	case image_mode_raw:
		break;
	}
	free(img->frames);
	free(img->id);
	color_space_unref(&img->cs);
}

static void raw_img_free_range(struct raw_img *img, const size_t start,
const size_t end) {
	for (size_t i = start; i < end; ++i) {
		raw_img_free(img + i);
	}
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

static void print_colorspace_data(const struct color_space *cs) {
	puts("  Colorspace:");
	printf("   Profile type: %s\n", color_space_type_str(cs));
	printf("   Range: %s\n", cs->limited ? "limited" : "full");
	printf("   Matrix: %s\n", cicp_matrix_str(cs->matrix));
	switch (cs->type) {
	case color_profile_enum:
		printf("   Transfer: %s\n",
			cicp_transfer_str(cs->transfer, cs->matrix));
		printf("   Primaries: %s\n", cicp_primaries_str(cs->primaries));
		break;
	case color_profile_custom:
		;const struct color_profile *prof = &cs->desc->u.prof;
		printf("   Gamma: %f %f %f\n", prof->xfer.r, prof->xfer.g,
			prof->xfer.b);
		const char *n[4] = {"White", "Red", "Green", "Blue"};
		const struct color_xy *p = (struct color_xy *)&prof->pri;
		puts("   Primaries:");
		for (size_t i = 0; i < 4; ++i) {
			printf("    %s: %f, %f\n", n[i], p[i].x, p[i].y);
		}
		break;
	case color_profile_icc:
		break;
	}
}

static void print_more_data(const struct raw_img *img) {
	printf("  Alignment: %d\n", img->alignment);
	printf("  Rotation: %d\n", img->rotate);
	printf("  Mirror: %s\n", img->mirror ? "yes" : "no");
	char layout[5] = "rgba";
	pix_layout_swizzle(layout, sizeof(layout) - 1, 1, img->layout);
	printf("  Pixel layout: %s\n", layout);

	print_colorspace_data(&img->cs);
	if (img->mode == image_mode_planar) {
		struct image_planes *p = img->u.planes;
		fputs("  Subsampling: ", stdout);
		for (int i = 0; i < img->channels; ++i) {
			printf("%hhu%hhu%c", p->p[i].x.subsamp, p->p[i].y.subsamp,
				(i == img->channels - 1) ? '\n' : ':');
		}
		printf("  Vertical alignment: %d\n", p->v_pad);
	}
}

static size_t print_dimensions(const struct raw_img *img) {
	printf("%zu x %zu x %d", img->w, img->h, img->channels);

	size_t memsize = 0;
	if (img->mode == image_mode_planar) {
		struct image_planes *planes = img->u.planes;
		struct plane_info *p = planes->p;
		fputs(" (planar)", stdout);
		for (int i = 0; i < img->channels; ++i) {
			memsize += p[i].size;
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
		printf(" (%s)", pix_attr_str(img->attr));
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
	case 0: max_x = 80; max_y = 16; break;
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
			printf(", \"%s\"", img->id);
		}
		fputs(", ", stdout);

		if (img->data) {
			overall_size += print_dimensions(img);
		} else {
			puts("Not loaded");
		}
		if (verbosity > 0) {
			print_more_data(img);
		}
	}

	if (file->nr > 1) {
		printf("Total size in memory: %zu\n", overall_size);
	}
}

void image_file_normalize(struct image_file *file) {
	if (!file->nr) {
		fatal_bug("Bad image", "No sub-images contained!");
	}/*
	for (size_t i = 0; i < file->nr; ++i) {
		struct raw_img *img = file->sub_img + i;
		if (!img->data) {
			continue;
		}
		raw_img_verify(img);
	}*/
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
	free(file->dec_state);
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
	const float max = 64.0f;
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
		return fit(w, h, (size_t)conf->fb.w, (size_t)conf->fb.h, max);
	}
	const size_t m = conf->max_img_size;
	return fit(w, h, m, m, max);
}

// SPDX-License-Identifier: 0BSD
#include <stdlib.h>
#include <string.h>

#include "misc/common.h"
#include "misc/math.h"
#include "misc/mem.h"
#include "raster/wuimg.h"

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

struct wu_tree * wuimg_get_metadata(struct wuimg *img) {
	if (!img->metadata) {
		img->metadata = tree_plant("Metadata");
	}
	return img->metadata;
}

void wuimg_aspect_ratio(struct wuimg *img, const int num, const int den) {
	if (num && den) {
		img->ratio = (float)num / (float)den;
	}
}

void wuimg_exif_orientation(struct wuimg *img, const int orientation) {
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

static void find_better_alignment(struct wuimg *img) {
	if (img->mode == image_mode_planar) {
		return;
	}
	const size_t w = img->w * img->channels;
	if (strip_padding(w, img->bitdepth, img->align_sh) < 8) {
		img->align_sh = 3;
	}
}

static size_t subsamp_dim(const size_t dim, struct plane_dim *s) {
	if (s->subsamp > 1) {
		return (dim + (s->subsamp - 1)) / s->subsamp;
	}
	s->subsamp = 1;
	return dim;
}

static size_t plane_calc_size(struct wuimg *img, const size_t i) {
	struct plane_info *p = img->u.planes->p + i;
	p->w = subsamp_dim(img->w, &p->x);
	p->h = subsamp_dim(img->h, &p->y);
	if (p->w < 1 || p->h < 1) {
		return 0;
	}
	p->stride = strip_length(p->w, img->bitdepth, img->align_sh);
	p->size = strip_length(p->h, 8, img->u.planes->v_pad) * p->stride;
	return p->size;
}

static const char * geom_verify(const uint8_t ch, const uint8_t bitdepth,
const enum pix_attr attr, const bool paletted) {
	if (!ch) {
		return "Channel number must not be zero";
	} else if (!bitdepth) {
		return "Bitdepth must not be zero";
	}

	if (paletted) {
		if (ch != 1) {
			return "Paletted images must use 1 channel";
		} else if (bitdepth > 8) {
			return "Paletted images must not use more than 8 bits";
		} else {
			switch (attr) {
			case pix_normal:
			case pix_inverted:
				break;
			case pix_signed:
				return "Paletted images can't use signed indices";
			case pix_float:
				return "Paletted images can't use floats";
			case pix_pack_332:
				return "Paletted images can't use 332 packing";
			case pix_pack_1555:
				return "Paletted images can't use 1555 packing";
			default:
				return "Undefined pixel attribute in paletted image";
			}
		}
	} else {
		const int depth = ch * bitdepth;
		switch (attr) {
		case pix_normal:
		case pix_signed:
		case pix_inverted:
		case pix_float:
			break;
		case pix_pack_332:
			if (depth != 8) {
				return "pix_pack_332 must be set with 1"
					"channel and 8 bits";
			}
			break;
		case pix_pack_1555:
			if (depth != 16) {
				return "pix_pack_1555 must be set with 1"
					"channel and 16 bits";
			}
			break;
		default:
			return "Undefined pixel attribute";
		}
	}
	return NULL;
}

static bool test_overflow_common(size_t w, const size_t h, const uint8_t ch,
const uint8_t bitdepth, const align_t align) {
	if (w > 0 && h > 0) {
		if (SIZE_MAX / w / ch > 1) {
			w *= ch;
			if (SIZE_MAX / w / bitdepth > 1) {
				size_t bytes = strip_base(w, bitdepth);
				const size_t a = ~0lu << align;
				if (SIZE_MAX - ~a >= bytes) {
					bytes = (bytes + ~a) & a;
					return SIZE_MAX / h / bytes > 1;
				}
			}
		}
	}
	return false;
}

static bool test_overflow(struct wuimg *img) {
	const uint8_t ch = (img->mode == image_mode_planar) ? 1 : img->channels;
	const bool ok = test_overflow_common(img->w, img->h, ch, img->bitdepth,
		img->align_sh);
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

enum wu_error wuimg_verify(struct wuimg *img) {
	const char *err_msg = geom_verify(img->channels, img->bitdepth,
		img->attr, img->mode == image_mode_palette);
	if (err_msg) {
		fatal_bug("Bad image", err_msg);
	}
	switch (img->attr) {
	case pix_pack_332:
		img->channels = 1;
		img->bitdepth = 8;
		break;
	case pix_pack_1555:
		img->channels = 1;
		img->bitdepth = 16;
		break;
	default:
		break;
	}
	if (img->align_sh > 3) {
		find_better_alignment(img);
	} else if (img->align_sh < 0) {
		return wu_invalid_params;
	}

	if (!test_overflow(img)) {
		return wu_int_overflow;
	}

	if (!img->used_bits) {
		img->used_bits = img->bitdepth;
	}

	if (!img->layout) {
		if (img->mode == image_mode_palette || img->channels >= 3
		|| img->attr == pix_pack_332 || img->attr == pix_pack_1555) {
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
	if (img->ratio == 0) {
		img->ratio = 1;
	}
	if (img->dec_scale == 0) {
		img->dec_scale = 1;
	}
	return wu_ok;
}

bool wuimg_exceeds_limit(const struct wuimg *img,
const struct wu_conf *wuconf) {
	return zumax(img->w, img->h) > wuconf->max_img_size;
}

size_t wuimg_stride(const struct wuimg *img) {
	return strip_length(img->w * img->channels, img->bitdepth,
		img->align_sh);
}

size_t wuimg_size(const struct wuimg *img) {
	if (img->mode == image_mode_planar) {
		size_t total = 0;
		for (uint8_t z = 0; z < img->channels; ++z) {
			total += img->u.planes->p[z].size;
		}
		return total;
	}
	return wuimg_stride(img) * img->h;
}

size_t wuimg_plane_resolve(struct wuimg *img) {
	size_t total = 0;
	for (uint8_t i = 0; i < img->channels; ++i) {
		total += plane_calc_size(img, i);
	}
	return total;
}

static void plane_alloc(struct wuimg *img) {
	const size_t total = wuimg_plane_resolve(img);
	img->data = calloc(1, total);
	if (img->data) {
		struct plane_info *p = img->u.planes->p;
		size_t pos = 0;
		for (uint8_t i = 0; i < img->channels; ++i) {
			p[i].ptr = img->data + pos;
			pos += p[i].size;
		}
	}
}

bool wuimg_alloc_noverify(struct wuimg *img) {
	if (img->mode == image_mode_planar) {
		plane_alloc(img);
	} else {
		img->data = calloc(1, wuimg_size(img));
	}
	return img->data;
}

enum wu_error wuimg_alloc(struct wuimg *img) {
	const enum wu_error st = wuimg_verify(img);
	if (st == wu_ok) {
		return wuimg_alloc_noverify(img) ? wu_ok : wu_alloc_error;
	}
	return st;
}

static void set_img_mode(struct wuimg *img, const enum image_mode mode) {
	if (img->mode != image_mode_raw) {
		fatal_bug("Bad image mode", "Image mode had been set previously");
	}
	img->mode = mode;
}

void wuimg_plane_position(struct wuimg *img, const int8_t horz,
const int8_t vert) {
	struct plane_info *p = img->u.planes->p;
	if (img->channels >= 2) {
		if (img->channels >= 3) {
			p[2].x.pos = horz;
			p[2].y.pos = vert;
		}
		p[1].x.pos = horz;
		p[1].y.pos = vert;
	}
}

void wuimg_plane_subsamp(struct wuimg *img, const uint8_t horz,
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

struct image_planes * wuimg_plane_init(struct wuimg *img) {
	struct image_planes *planes = calloc(sizeof(*planes)
		+ img->channels * sizeof(*planes->p), 1);
	if (planes) {
		img->u.planes = planes;
		set_img_mode(img, image_mode_planar);
	}
	return planes;
}

struct raster_pal * wuimg_palette_set(struct wuimg *img,
struct raster_pal *pal) {
	if (pal) {
		set_img_mode(img, image_mode_palette);
		img->u.palette = pal;
	}
	return pal;
}

struct raster_pal * wuimg_palette_init(struct wuimg *img) {
	return wuimg_palette_set(img,  calloc(1, sizeof(struct raster_pal)));
}

int wuimg_frame_prev_keyframe(struct wuimg *img, const int current, int i) {
	const int limit = current <= i ? current : 0;
	while (i > limit && !img->frames->f[i].keyframe) {
		--i;
	}
	return i;
}

bool wuimg_frame_set(struct wuimg *img, const size_t i, const size_t x,
const size_t y, const size_t w, const size_t h, const int msec,
const bool opaque) {
	img->frames->f[i] = (struct frame_info) {
		.x = x, .y = y, .w = w, .h = h,
		.msec = msec,
		.keyframe = (opaque && !x && !y && w == img->w && h == img->h),
	};
	return compost_bounds_check(img->w, img->h, img->frames->f + i);
}

size_t wuimg_frames_nr(const struct wuimg *img) {
	return img->frames ? img->frames->nr : 1;
}

struct image_frames * wuimg_frames_init(struct wuimg *img, size_t nr) {
	if (nr < 1) {
		return false;
	}
	img->frames = calloc(sizeof(*img->frames) + nr * sizeof(*img->frames->f), 1);
	if (img->frames) {
		img->frames->nr = nr;
	}
	return img->frames;
}

void wuimg_align(struct wuimg *img, const uint8_t alignment) {
	img->align_sh = align_from_int(alignment);
}

bool wuimg_clone(struct wuimg *dst, struct wuimg *src) {
	*dst = *src;
	dst->data = NULL;
	dst->frames = NULL;
	dst->mode = image_mode_raw;
	dst->cs = color_space_ref(&src->cs);
	dst->metadata = NULL;
	switch (src->mode) {
	case image_mode_raw: break;
	case image_mode_palette:
		return wuimg_palette_set(dst,
			memdup(src->u.palette, sizeof(*src->u.palette)));
	case image_mode_planar:
		return wuimg_plane_init(dst);
	}
	return true;
}

void wuimg_free(struct wuimg *img) {
	if (!img->borrowed) {
		free(img->data);
	}
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
	if (img->metadata) {
		tree_unroot(img->metadata);
		free(img->metadata);
	}
	color_space_unref(&img->cs);
}

void wuimg_clear(struct wuimg *img) {
	wuimg_free(img);
	memset(img, 0, sizeof(*img));
}


static void print_colorspace_data(const struct color_space *cs) {
	printf("  Colorspace:\n"
		"   Type: %s\n"
		"   Range: %s\n"
		"   Matrix: %s\n",
		color_space_type_str(cs),
		cs->limited ? "limited" : "full",
		cicp_matrix_str(cs->matrix));
	switch (cs->type) {
	case color_profile_enum:
		printf("   Transfer: %s\n"
			"   Primaries: %s\n",
			cicp_transfer_str(cs->transfer, cs->matrix),
			cicp_primaries_str(cs->primaries));
		break;
	case color_profile_custom:
		;const struct color_profile *prof = &cs->desc->u.prof;
		printf("   Gamma: %f %f %f\n", prof->gamma.r, prof->gamma.g,
			prof->gamma.b);
		puts("   Primaries:");
		const struct color_xy *p = (struct color_xy *)&prof->pri;
		const char *n[4] = {"White", "Red", "Green", "Blue"};
		for (size_t i = 0; i < 4; ++i) {
			printf("    %s: %f, %f\n", n[i], p[i].x, p[i].y);
		}
		break;
	case color_profile_icc:
		return;
	}
}

static void print_more_data(const struct wuimg *img, const int verbosity) {
	fputs("  Layout: ", stdout);
	pix_layout_print(img->layout, stdout);
	printf("  Alignment: %d\n"
		"  Rotation: %d\n"
		"  Mirror: %s\n"
		"  Alpha: %d\n"
		"  Ratio: %g\n"
		"  Bits used: %d\n",
		img->align_sh, img->rotate, img->mirror ? "yes" : "no",
		img->alpha, img->ratio, img->used_bits);

	print_colorspace_data(&img->cs);
	if (img->mode == image_mode_planar) {
		const struct image_planes *planes = img->u.planes;
		const struct plane_info *p = planes->p;
		fputs("  Planes:\n", stdout);
		printf("   Vertical alignment: %d\n", planes->v_pad);
		if (verbosity > 2) {
			for (int i = 0; i < img->channels; ++i) {
				printf("   Plane %d:\n"
					"    Subsampling: %d:%d\n"
					"    Positioning: %d:%d\n"
					"    Width: %zu\n"
					"    Height: %zu\n"
					"    Stride: %zu\n",
					i,
					p[i].x.subsamp, p[i].y.subsamp,
					p[i].x.pos, p[i].y.pos,
					p[i].w, p[i].h, p[i].stride);
			}
		} else {
			fputs("   Subsampling: ", stdout);
			for (int i = 0; i < img->channels; ++i) {
				printf("%d:%d%c", p[i].x.subsamp, p[i].y.subsamp,
					(i == img->channels - 1) ? '\n' : '/');
			}
			fputs("   Positioning: ", stdout);
			for (int i = 0; i < img->channels; ++i) {
				printf("%d:%d%c", p[i].x.pos, p[i].y.pos,
					(i == img->channels - 1) ? '\n' : '/');
			}
		}
	}
	if (img->metadata) {
		const size_t max_x = verbosity > 2 ? SIZE_MAX : 80;
		const size_t max_y = verbosity > 2 ? SIZE_MAX : 24;
		tree_print(img->metadata, max_x, max_y, 2, stdout);
	}
}

static size_t print_dimensions(const struct wuimg *img) {
	const char *mode_str = "";
	switch (img->mode) {
	case image_mode_raw: break;
	case image_mode_palette: mode_str = " (paletted)"; break;
	case image_mode_planar: mode_str = " (planar)"; break;
	}

	printf("%zu x %zu x %d%s x %d ",
		img->w, img->h, img->channels, mode_str, img->bitdepth);
	if (img->attr) {
		printf("(%s) ", pix_attr_str(img->attr));
	}

	const size_t memsize = wuimg_size(img);
	printf("= %zu bytes", memsize);
	if (img->dec_scale != 1) {
		printf(", %.2fx original", img->dec_scale);
	}
	putchar('\n');
	return memsize;
}

size_t wuimg_print(const struct wuimg *img, const int verbosity) {
	if (verbosity < 1) {
		return wuimg_size(img);
	}

	if (img->metadata) {
		fputs("(*) ", stdout);
	}
	if (img->frames) {
		printf("frames: %zu, ", img->frames->nr);
	}

	size_t size = 0;
	if (img->data || (img->mode == image_mode_planar && img->u.planes)) {
		size = print_dimensions(img);
	} else {
		puts("Not loaded");
	}
	if (verbosity > 1) {
		print_more_data(img, verbosity);
	}
	return size;
}

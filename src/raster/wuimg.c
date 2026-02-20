// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2019 kaleido
#include <stdlib.h>
#include <string.h>

#include "misc/common.h"
#include "misc/math.h"
#include "misc/mem.h"
#include "raster/wuimg.h"

struct wutree * wuimg_get_metadata(struct wuimg *img) {
	if (!img->metadata) {
		img->metadata = tree_plant("Metadata");
	}
	return img->metadata;
}

void wuimg_aspect_ratio(struct wuimg *img, const unsigned h_size,
const unsigned v_size) {
	if (h_size && v_size) {
		img->ratio = (float)h_size / (float)v_size;
	}
}

void wuimg_exif_orientation(struct wuimg *img, const int orientation) {
	// https://www.awaresystems.be/imaging/tiff/tifftags/orientation.html
	bool m;
	uint8_t r;
	switch (orientation) { //      1st row / 1st column
	case 1: m = 0; r = 0; break; //    Top / left
	case 2: m = 1; r = 2; break; //    Top / right
	case 3: m = 0; r = 2; break; // Bottom / right
	case 4: m = 1; r = 0; break; // Bottom / left
	case 5: m = 1; r = 3; break; //   Left / top
	case 6: m = 0; r = 1; break; //  Right / top
	case 7: m = 1; r = 1; break; //  Right / bottom
	case 8: m = 0; r = 3; break; //   Left / bottom
	default: return; // Reserved
	}
	img->mirror = m;
	img->rotate = r;
}

static size_t subsamp_dim(const size_t dim, struct plane_dim *s) {
	if (s->subsamp > 1) {
		return (dim + (s->subsamp - 1)) / s->subsamp;
	}
	s->subsamp = 1;
	return dim;
}

static bool bad_cosit(const struct plane_dim s) {
	return s.cosit && s.subsamp != 2;
}

static size_t plane_calc_size(struct wuimg *img, const size_t i) {
	struct plane_info *p = img->u.planes->p + i;
	p->w = subsamp_dim(img->w, &p->x);
	p->h = subsamp_dim(img->h, &p->y);
	if (p->w < 1 || p->h < 1) {
		return 0;
	} else if (bad_cosit(p->x) || bad_cosit(p->y)) {
		fatal_bug(__func__, "Cositing is only used for subsampling == 2");
	}
	p->stride = strip_length(p->w, img->bitdepth, img->align_sh);
	p->size = strip_length(p->h, 8, img->u.planes->v_pad) * p->stride;
	return p->size;
}

static const char * geom_verify(const uint8_t ch, const uint8_t bitdepth,
const align_t align, const enum pix_attr attr, const enum image_mode mode) {
	if (!bitdepth) {
		return "Bitdepth must not be zero";
	} else if (align <= align_error) {
		return "Invalid alignment";
	}

	switch (mode) {
	case image_mode_planar:
		if (align == align_bitpack) {
			return "Bitpacked planar images unsupported";
		}
		// fallthrough
	case image_mode_raw:
		if (!ch) {
			return "Channel number must not be zero for raw or"
				" planar images";
		} else if (attr == pix_float) {
			switch (bitdepth) {
			case 16: case 32: case 64: break;
			default: return "Float depth must be 16, 32, or 64";
			}
		}
		return NULL;
	case image_mode_palette:
		if (ch > 1) {
			return "Paletted images must use 1 channel";
		} else if (bitdepth > 8) {
			return "Paletted images must not use more than 8 bits";
		}
		break;
	case image_mode_bitfield:
		if (align == align_bitpack) {
			return "Bitpacked bitfield unsupported";
		} else if (ch > 1) {
			return "Bitfield images must use 1 channel";
		}
		break;
	}
	switch (attr) {
	case pix_normal:
		return NULL;
	case pix_signed:
	case pix_float:
		return "Only raw and planar images can use attributes";
	}
	return "Undefined pixel attribute";
}

static bool test_overflow_common(size_t w, const size_t h, const uint8_t ch,
const uint8_t bitdepth, const align_t align) {
	if (w > 0 && h > 0) {
		if (SIZE_MAX / w / ch > 1) {
			w *= ch;
			if (SIZE_MAX / w / bitdepth > 1) {
				size_t bytes = strip_base(w, bitdepth);
				const size_t a = ~0lu << imax(align, 0);
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
		img->align_sh, img->attr, img->mode);
	if (err_msg) {
		fatal_bug(__func__, err_msg);
	}

	if (!img->channels) {
		img->channels = 1;
	}

	if (!test_overflow(img)) {
		return wu_int_overflow;
	}

	const uint8_t depth = img->mode == image_mode_palette ? 8 : img->bitdepth;
	if (img->bitrange > depth) {
		fatal_bug(__func__, "bitrange can't be greater than bitdepth,"
			" or 8 for paletted images");
	} else if (!img->bitrange) {
		img->bitrange = img->mode == image_mode_bitfield
			? img->u.bitfield->outdepth : depth;
	}

	if (!img->layout) {
		if (img->mode == image_mode_palette
		|| img->mode == image_mode_bitfield
		|| img->channels >= 3) {
			img->layout = pix_rgba;
		} else {
			img->layout = pix_gray;
		}
	}
	if (img->mode != image_mode_palette) {
		const uint8_t a = pix_layout_offset(img->layout, pix_alpha);
		const uint8_t ch = img->mode == image_mode_bitfield
			? img->u.bitfield->ch : img->channels;
		if (a >= ch) {
			img->alpha = alpha_ignore;
		}
	}
	if (img->ratio == 0) {
		img->ratio = 1;
	}
	return wu_ok;
}

struct wu_st wuimg_verify_st(struct wuimg *img) {
	return wuerr(wuimg_verify(img), NULL);
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
	} else if (img->align_sh == align_bitpack) {
		return strip_length(img->w * img->channels * img->h,
			img->bitdepth, 0);
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
	if (img->data) {
		fatal_bug("wuimg_alloc()", "image was already allocated");
	}
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

enum wu_error wuimg_alloc_limit(struct wuimg *img, const struct wu_conf *conf) {
	enum wu_error e = wu_exceeds_size_limit;
	if (!wuimg_exceeds_limit(img, conf)) {
		e = wuimg_verify(img);
		if (e == wu_ok && !wuimg_alloc_noverify(img)) {
			e = wu_alloc_error;
		}
	}
	return e;
}


static void * set_img_mode(struct wuimg *img, const enum image_mode mode,
void *restrict data) {
	if (img->mode != image_mode_raw) {
		fatal_bug("Bad image mode", "Image mode had been set previously");
	}
	img->mode = mode;
	img->u.palette = data;
	return data;
}


struct bitfield * wuimg_bitfield_init(struct wuimg *img) {
	return set_img_mode(img, image_mode_bitfield,
		calloc(1, sizeof(*img->u.bitfield)));
}

struct bitfield * wuimg_bitfield_from_id(struct wuimg *img, const uint16_t id) {
	struct bitfield *bf = wuimg_bitfield_init(img);
	if (bf) {
		bitfield_from_id(bf, id, img->bitdepth);
	}
	return bf;
}

enum wu_error wuimg_bitfield_from_mask(struct wuimg *img,
const uint32_t *mask, const uint8_t ch, const uint8_t word_depth) {
	struct bitfield *bf = wuimg_bitfield_init(img);
	if (bf) {
		enum pix_layout l = bitfield_from_mask(bf, mask, ch, word_depth);
		if (l) {
			img->layout = pix_layout_mul(l,
				img->layout ? img->layout : pix_rgba);
			return wu_ok;
		}
		return wu_invalid_header;
	}
	return wu_alloc_error;
}


static void set_cosit(struct plane_dim *s, const bool cosit) {
	s->cosit = s->subsamp == 2 ? cosit : false;
}

void wuimg_plane_cosit(struct wuimg *img, const bool horz,
const bool vert) {
	struct plane_info *p = img->u.planes->p;
	if (img->channels >= 2) {
		if (img->channels >= 3) {
			set_cosit(&p[2].x, horz);
			set_cosit(&p[2].y, vert);
		}
		set_cosit(&p[1].x, horz);
		set_cosit(&p[1].y, vert);
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
	return set_img_mode(img, image_mode_planar,
		calloc(1, sizeof(*img->u.planes)
			+ img->channels * sizeof(*img->u.planes->p)));
}


struct palette * wuimg_palette_set(struct wuimg *img, struct palette *pal) {
	return set_img_mode(img, image_mode_palette, pal);
}

struct palette * wuimg_palette_init(struct wuimg *img) {
	return set_img_mode(img, image_mode_palette, palette_new());
}

struct wu_st wuimg_palette_from_file(struct wuimg *img, uint8_t size,
size_t nmemb, FILE *ifp) {
	struct palette *pal = wuimg_palette_init(img);
	if (pal) {
		if (!img->bitrange) {
			img->bitrange = 8;
		}
		return palette_from_file(pal, size, nmemb, ifp, img->bitrange)
			? WU_OK : WUERR_HERE(wu_unexpected_eof);
	}
	return WUERR_HERE(wu_alloc_error);
}

struct wu_st wuimg_palette_from_buf(struct wuimg *img, uint8_t size,
size_t nmemb, const uint8_t *src) {
	struct palette *pal = wuimg_palette_init(img);
	if (pal) {
		if (!img->bitrange) {
			img->bitrange = 8;
		}
		if (size == 3) {
			palette_from_rgb8_bitrange(pal, src, nmemb,
				img->bitrange);
		} else {
			memcpy(pal->color, src, size*nmemb);
		}
		return WU_OK;
	}
	return WUERR_HERE(wu_alloc_error);
}


int wuimg_frame_prev_nearest(struct wuimg *img, const int shown, int i) {
	const int limit = i < shown ? 0 : shown + 1;
	while (i > limit && !img->frames->f[i].keyframe) {
		--i;
	}
	return i;
}

bool wuimg_frame_set(struct wuimg *img, const size_t i, const size_t x,
const size_t y, const size_t w, const size_t h, const uint32_t sec_num,
const uint32_t sec_den, const bool independent) {
	img->frames->f[i] = (struct frame_info) {
		.reg = {
			.x = x, .y = y,
			.w = w, .h = h,
		},
		.sec = {
			.num = sec_num,
			.den = sec_den,
		},
		.keyframe = (independent && !x && !y && w == img->w && h == img->h),
	};
	return compost_bounds_check(img->w, img->h, &img->frames->f[i].reg);
}

size_t wuimg_frames_nr(const struct wuimg *img) {
	return img->frames ? img->frames->nr : 1;
}

struct image_frames * wuimg_frames_init(struct wuimg *img, size_t nr) {
	if (nr < 1) {
		return false;
	}
	img->frames = small_calloc(sizeof(*img->frames) + nr * sizeof(*img->frames->f), 1);
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
	dst->metadata = NULL;
	bool ok = false;
	switch (src->mode) {
	case image_mode_raw:
		ok = true;
		break;
	case image_mode_palette:
		ok = wuimg_palette_set(dst, palette_copy(src->u.palette));
		break;
	case image_mode_planar:
		ok = wuimg_plane_init(dst);
		break;
	case image_mode_bitfield:
		ok = set_img_mode(dst, image_mode_bitfield,
			memdup(src->u.bitfield, sizeof(*src->u.bitfield)));
		break;
	}
	if (ok) {
		dst->cs = color_space_ref(&src->cs);
	}
	return ok;
}

void wuimg_free(struct wuimg *img) {
	if (!img->borrowed) {
		free(img->data);
	}
	switch (img->mode) {
	case image_mode_bitfield:
		free(img->u.bitfield);
		break;
	case image_mode_planar:
		free(img->u.planes);
		break;
	case image_mode_palette:
		palette_unref(img->u.palette);
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


bool wuimg_has_data(const struct wuimg *img) {
	return img->data || (img->mode == image_mode_planar && img->u.planes);
}

static void print_colorspace_data(const struct color_space *cs, FILE *out) {
	fprintf(out,
		"  Colorspace:\n"
		"   Type: %s\n"
		"   Range: %s%s%s\n"
		"   Matrix: %s\n",
		color_space_type_str(cs),
		cs->limited ? "limited" : "full",
		cs->invert ? ", inverted color" : "",
		cs->invert_alpha ? ", inverted alpha" : "",
		cicp_matrix_str(cs->matrix));
	switch (cs->type) {
	case color_profile_enum:
	case color_profile_param:
		if (cs->transfer == 0 && cs->type == color_profile_param) {
			const struct color_profile *prof = cs->desc.prof;
			fprintf(out, "   Gamma: %f %f %f\n",
				prof->gamma.r, prof->gamma.g, prof->gamma.b);
		} else {
			fprintf(out, "   Transfer: %s\n",
				cicp_transfer_str(cs->transfer, cs->matrix));
		}
		if (cs->primaries == 0 && cs->type == color_profile_param) {
			fputs("   Primaries:\n", out);
			const struct color_xy *p =
				(const struct color_xy *)&cs->desc.prof->pri;
			const char *n[4] = {"White", "Red", "Green", "Blue"};
			for (size_t i = 0; i < 4; ++i) {
				fprintf(out, "    %s: %f, %f\n",
					n[i], p[i].x, p[i].y);
			}
		} else {
			fprintf(out, "   Primaries: %s\n",
				cicp_primaries_str(cs->primaries));
		}
		break;
	case color_profile_icc:
		break;
	}
}

static const char * alpha_str(enum alpha_interpretation alpha) {
	switch (alpha) {
	case alpha_unassociated: return "Unassociated";
	case alpha_associated: return "Associated";
	case alpha_key: return "Key";
	case alpha_ignore: return "Ignored";
	}
	return "???";
}

static void print_more_data(const struct wuimg *img, FILE *out,
const int verbosity) {
	fputs("  Layout: ", out);
	pix_layout_print(img->layout, out);
	fprintf(out,
		"  Alignment: %d\n"
		"  Rotation: %s%d\n"
		"  Alpha: %s\n"
		"  Pixel ratio: %g\n"
		"  Bits used: %d\n",
		img->align_sh,
		img->mirror ? "vflip " : "",
		img->rotate,
		alpha_str(img->alpha), img->ratio, img->bitrange);

	print_colorspace_data(&img->cs, out);
	if (img->mode == image_mode_planar) {
		const struct image_planes *planes = img->u.planes;
		const struct plane_info *p = planes->p;
		fputs("  Planes:\n", out);
		fprintf(out, "   Vertical alignment: %d\n", planes->v_pad);
		if (verbosity > 2) {
			for (int i = 0; i < img->channels; ++i) {
				fprintf(out,
					"   Plane %d:\n"
					"    Subsampling: %d:%d\n"
					"    Cositing: %d:%d\n"
					"    Dimensions: %zu x %zu\n"
					"    Stride: %zu\n",
					i,
					p[i].x.subsamp, p[i].y.subsamp,
					p[i].x.cosit, p[i].y.cosit,
					p[i].w, p[i].h, p[i].stride);
			}
		} else {
			fputs("   Subsampling: ", out);
			for (int i = 0; i < img->channels; ++i) {
				fprintf(out, "%d:%d%c",
					p[i].x.subsamp, p[i].y.subsamp,
					(i == img->channels - 1) ? '\n' : '/');
			}
			fputs("   Cositing: ", out);
			for (int i = 0; i < img->channels; ++i) {
				fprintf(out, "%d:%d%c",
					p[i].x.cosit, p[i].y.cosit,
					(i == img->channels - 1) ? '\n' : '/');
			}
		}
	}
	if (img->metadata) {
		const size_t max_x = verbosity > 2 ? SIZE_MAX : 80;
		const size_t max_y = verbosity > 2 ? SIZE_MAX : 24;
		tree_print(img->metadata, max_x, max_y, 2, out);
	}
}

static void print_dimensions(const struct wuimg *img, FILE *out,
const size_t memsize) {
	char bf_str[4 + sizeof(uint16_t)*2];
	const char *mode_str = "";
	switch (img->mode) {
	case image_mode_raw: break;
	case image_mode_palette: mode_str = " (paletted)"; break;
	case image_mode_planar: mode_str = " (planar)"; break;
	case image_mode_bitfield:
		snprintf(bf_str, sizeof(bf_str), " (%x)",
			(uint16_t)img->u.bitfield->id);
		mode_str = bf_str;
		break;
	}

	fprintf(out, "%zu x %zu x %d%s x %d ",
		img->w, img->h, img->channels, mode_str, img->bitdepth);
	if (img->attr) {
		fprintf(out, "(%s) ", pix_attr_str(img->attr));
	}
	fprintf(out, "= %zu bytes%s", memsize,
		wuimg_has_data(img) ? "" : " (Not loaded)");
}

size_t wuimg_print(const struct wuimg *img, FILE *out, const int verbosity,
const char *short_end) {
	const size_t memsize = wuimg_size(img);
	if (verbosity > 0) {
		if (img->metadata) {
			fputs("(*) ", out);
		}
		if (img->frames) {
			fprintf(out, "frames: %zu, ", img->frames->nr);
		}
		print_dimensions(img, out, memsize);
		if (verbosity > 1) {
			fputc('\n', stdout);
			print_more_data(img, out, verbosity);
		} else {
			fputs(short_end, out);
		}
	}
	return memsize;
}

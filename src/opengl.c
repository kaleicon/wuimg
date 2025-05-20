// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2019 kaleido
#include "opengl.h"
#include "misc/bit.h"
#include "misc/common.h"
#include "misc/endian.h"
#include "misc/math.h"
#include "raster/color.h"
#include "raster/pix.h"
#include "raster/strip.h"
#include "raster/unpack.h"

/* GLSL variables */
#define ATTR_POS "pos"

#define UNI_IMG "img"
#define UNI_PAL "pal"
#define UNI_PLANE3 "plane3"
#define UNI_PLANE_ALPHA "plane4"
#define UNI_CMS_LUT "cms_lut"

#define UNI_MAT_POS "mat_pos"
#define UNI_MAT_NONLINEAR "mat_nonlinear"
#define UNI_MAT_CMS "mat_cms"
#define UNI_MODE_COLOR "mode_color"
#define UNI_MODE_ALPHA "mode_alpha"
#define UNI_MODE_CMS "mode_cms"
#define UNI_EOTF_FN "eotf_fn"
#define UNI_EOTF_ARGS "eotf_args"
#define UNI_POSITIONING "posit"
#define UNI_REMAP "remap"

#define COLOR_RAW "0"
#define COLOR_PALETTE "1"
#define COLOR_PLANAR "2"

#define ALPHA_COLOR_MULTIPLY "0"
#define ALPHA_COLOR_NO_MULTIPLY "1"
#define ALPHA_COLOR_UNMULTIPLY "2"

#define ALPHA_BG_NONE "0"
#define ALPHA_BG_ONE "1"
#define ALPHA_BG_CHECKERS "2"

#define CMS_NONE "0"
#define CMS_SPACEWALK "1"
#define CMS_LUT "2"

#define EOTF_LINEAR_GAMMA "0"
#define EOTF_PQ "1"
#define EOTF_HLG "2"

static const float VISUAL_EPSILON = 0x1p-16;
static const align_t DEFAULT_ALIGN = 2;

enum gl_mag_filter {
	gl_mag_linear = GL_LINEAR,
	gl_mag_nearest = GL_NEAREST,
};

enum gl_min_filter {
	gl_min_linear = GL_LINEAR_MIPMAP_LINEAR,
	gl_min_nearest = GL_NEAREST,
};

enum gl_tex_unit {
	gl_tex_img = 0,
	gl_tex_pal,
	gl_tex_plane3,
	gl_tex_plane_alpha,
	gl_tex_cms,
	gl_tex_reader,
};

struct gl_tex_params {
	GLint in_fmt;
	GLenum fmt, type;
};

struct gl_upload_params {
	struct gl_tex_params tex;
	enum pix_layout layout:8;
	enum unpack_op op:8;
	uint8_t comps;
	uint8_t bd;
};

const char * gl_strerror(const GLenum error) {
	switch (error) {
	case GL_NO_ERROR:
		return "No error";
	case GL_INVALID_ENUM:
		return "Invalid enum";
	case GL_INVALID_VALUE:
		return "Invalid value";
	case GL_INVALID_OPERATION:
		return "Invalid operation";
	case GL_INVALID_FRAMEBUFFER_OPERATION:
		return "Invalid framebuffer operation";
	case GL_OUT_OF_MEMORY:
		return "Out of memory";
	case GL_STACK_UNDERFLOW:
		return "Stack underflow";
	case GL_STACK_OVERFLOW:
		return "Stack overflow";
	}
	return "???";
}

watch_t gl_clock_query(const struct gl_context *context) {
	GLuint64 ns = 0;
	glGetQueryObjectui64v(context->timer, GL_QUERY_RESULT, &ns);
	return ns;
}

static void gl_clock_end(void) {
	glEndQuery(GL_TIME_ELAPSED);
}

static void gl_clock_start(const struct gl_context *context) {
	glBeginQuery(GL_TIME_ELAPSED, context->timer);
}

void gl_terminate(struct gl_context *context) {
	if (context->icc) {
		cmsCloseProfile(context->icc);
	}
}

static void set_alpha_ops(const struct gl_context *context) {
	const int alpha = (context->user_alpha + context->tex.alpha) % 6;
	GLint ops[2];
	if (context->unmultiply) {
		ops[0] = (alpha == alpha_associated) ? 2 : alpha_no_multiply;
	} else {
		ops[0] = alpha & alpha_no_multiply;
	}
	ops[1] = alpha >> 1;
	glUniform1iv(context->uni.mode.alpha, ARRAY_LEN(ops), ops);
}

void gl_alpha_toggle(struct gl_context *context, const int cycle) {
	context->user_alpha = (uint8_t)imod(context->user_alpha + cycle, 6);
	context->update = gl_update_redraw;
	set_alpha_ops(context);
}

static void tex_active(const enum gl_tex_unit unit) {
	glActiveTexture(GL_TEXTURE0 + (GLenum)unit);
}

static void tex_2d_parameteri(const GLenum name, const GLint param) {
	glTexParameteri(GL_TEXTURE_2D, name, param);
}

static void tex_2d_mag(const enum gl_mag_filter filter) {
	tex_2d_parameteri(GL_TEXTURE_MAG_FILTER, (GLint)filter);
}

static void set_mag_filter(const unsigned is_subsamp, const bool good) {
	for (enum gl_tex_unit i = gl_tex_img; i <= gl_tex_plane_alpha; ++i) {
		tex_active(i);
		tex_2d_mag((good || ((is_subsamp >> i) & 1))
			? gl_mag_linear : gl_mag_nearest);
	}
	tex_active(gl_tex_img);
}

static void fix_aspect_ratio(struct mat3f *mat, struct gl_context *context,
const int rotate) {
	/* Scale the image to its natural size and aspect ratio, taking
	 * rotation into account.
	 * 'horz' and 'vert' should always be greater than 1, so that no
	 * pixels are hidden at 1x. */
	const float horz = fmaxf(context->tex.ratio, 1);
	const float vert = fmaxf(1/context->tex.ratio, 1);

	const int r1 = rotate & 1;
	const float w = context->tex.w * horz * context->pix_size[r1];
	const float h = context->tex.h * vert * context->pix_size[r1^1];

	if (mat) {
		const int r2 = 4 - r1;
		mat->m[r1] *= w;
		mat->m[r2] *= h;
	}
	context->tex.fit_zoom = 1 / fmaxf(w, h);
}

static void calc_fit_zoom(struct gl_context *context) {
	fix_aspect_ratio(NULL, context, context->tex.rotate);
}

static int bool_to_sign(const bool val) {
	return (val << 1) - 1;
}

static int hard_math(const int rotate) {
	return (rotate & 1) * bool_to_sign(rotate & 2);
}

static struct mat2i mat_mirrot(const int rotate, const bool mirror) {
	const int cosy = hard_math(rotate - 1);
	const int sinner = hard_math(rotate);
	const int mirror_mul = bool_to_sign(mirror);
	return (struct mat2i) {{
		cosy, sinner,
		sinner * mirror_mul, -cosy * mirror_mul
	}};
}

static void set_mirrot(struct mat3f *dst, const struct gl_image_info *tex,
const int rotate, const bool mirror) {
	/* GL textures are bottom-up, so negate mirror here to flip to
	 * top-down without anyone knowing. */
	const struct mat2i image = mat_mirrot(tex->rotate, !tex->mirror);
	const struct mat2i user = mat_mirrot(rotate, mirror);
	struct mat2i final;
	mati_mul(final.m, image.m, user.m, 2, 2, 2);
	dst->m[0] = (GLfloat)final.m[0];
	dst->m[1] = (GLfloat)final.m[1];
	dst->m[3] = (GLfloat)final.m[2];
	dst->m[4] = (GLfloat)final.m[3];
}

static void matrix_update(struct gl_context *context,
const struct wu_state *state) {
	const bool nt = context->tex.no_transform;

	const int rotate = nt ? 0 : state->rotate;
	const bool mirror = nt ? 0 : state->mirror;
	const float zoom = nt ? 1 : state->zoom;
	const float x = nt ? 0 : state->x_offset;
	const float y = nt ? 0 : state->y_offset;

	struct mat3f mat = {0};

	set_mirrot(&mat, &context->tex, rotate, mirror);
	fix_aspect_ratio(&mat, context, context->tex.rotate + rotate);

	/* Offsets are measured in pixels, but for the matrix a doubling is
	 * needed for some reason I've lost track of. */
	const float scale = 2;
	/* Using exact integer offsets causes ugly artifacts when rendering.
	 * We add a fraction of a pixel to fix this. */
	const float fix = 0x1p-16;
	mat.m[6] = fmaf( x * context->pix_size[0], scale, fix);
	mat.m[7] = fmaf(-y * context->pix_size[1], scale, fix);
	mat.m[8] = 1 / zoom;
	glUniformMatrix3fv(context->uni.mat.pos, 1, GL_FALSE, mat.m);

	if (context->tex.mode != image_mode_palette) {
		set_mag_filter(context->tex.subsamp,
			1.0f + VISUAL_EPSILON < zoom
			&& zoom < 2.0f - VISUAL_EPSILON);
	}
}

bool gl_draw(struct gl_context *context, const struct wu_state *state) {
	switch (context->update) {
	case gl_update_matrix:
		matrix_update(context, state);
		// fallthrough
	case gl_update_redraw:
		gl_clock_start(context);
		glClear(GL_COLOR_BUFFER_BIT);
		glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
		gl_clock_end();
		context->update = gl_update_none;
		return true;
	case gl_update_none:
		break;
	}
	return false;
}

void gl_clear_color(const uint8_t bg[static 4]) {
	const float scale = 1.0f / UCHAR_MAX;
	const float alpha = (float)bg[3] * scale;
	float rgb[3];
	for (size_t i = 0; i < ARRAY_LEN(rgb); ++i) {
		rgb[i] = bg[i] * scale * alpha;
	}
	glClearColor(rgb[0], rgb[1], rgb[2], alpha);
}

void gl_viewport(struct gl_context *context, const struct display_dims *dims) {
	context->pix_size[0] = (float)(1.0/dims->w);
	context->pix_size[1] = (float)(1.0/dims->h);
	calc_fit_zoom(context);
	context->update = gl_update_matrix;
	glViewport(0, 0, (int)dims->w, (int)dims->h);
}

static void tex_filter(const GLenum target, const GLint level,
const enum gl_min_filter min, const enum gl_mag_filter mag) {
	glTexParameteri(target, GL_TEXTURE_MAX_LEVEL, level);
	glTexParameteri(target, GL_TEXTURE_MIN_FILTER, min);
	glTexParameteri(target, GL_TEXTURE_MAG_FILTER, mag);
}

static void tex_2d_swizzle(const enum pix_layout layout) {
	GLint swz[] = {GL_RED, GL_GREEN, GL_BLUE, GL_ALPHA};
	pix_layout_swizzle(swz, sizeof(*swz), ARRAY_LEN(swz), layout);
	glTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_RGBA, swz);
}

static void tex_2d(const GLint in_fmt, const GLsizei w,
const GLsizei h, const GLenum fmt, const GLenum type, const void *ptr) {
	glTexImage2D(GL_TEXTURE_2D, 0, in_fmt, w, h, 0, fmt, type, ptr);
	glGenerateMipmap(GL_TEXTURE_2D);
}

static void tex_sub2d(const GLint x, const GLint y, const GLsizei w,
const GLsizei h, const GLenum fmt, const GLenum type, const void *ptr) {
	glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, w, h, fmt, type, ptr);
	glGenerateMipmap(GL_TEXTURE_2D);
}

static void tex_2d_params(const struct gl_upload_params *p, const size_t w,
const size_t h, const void *data) {
	tex_2d(p->tex.in_fmt, (GLsizei)w, (GLsizei)h, p->tex.fmt, p->tex.type,
		data);
	tex_2d_swizzle(p->layout);
}

static void tex_2d_null(void) {
	tex_2d(GL_RED, 0, 0, GL_RED, GL_UNSIGNED_BYTE, NULL);
}

static void tex_2d_solid(void) {
	const uint8_t c = 0xff;
	tex_2d(GL_RED, 1, 1, GL_RED, GL_UNSIGNED_BYTE, &c);
}

static void bind_buffer_data(const GLenum target, const GLuint buffer,
const size_t size, const GLvoid *data, const GLenum usage) {
	glBindBuffer(target, buffer);
	glBufferData(target, (GLsizeiptr)size, data, usage);
}

static void * map_unpack_buffer(const GLuint pix_buf, const size_t size,
const GLenum access) {
	bind_buffer_data(GL_PIXEL_UNPACK_BUFFER, pix_buf, size, NULL,
		GL_STREAM_DRAW);
	return glMapBuffer(GL_PIXEL_UNPACK_BUFFER, access);
}

static void palette_parameters(const bool enable) {
	tex_active(gl_tex_pal);
	GLint level;
	enum gl_min_filter min;
	if (enable) {
		level = 0;
		min = gl_min_nearest;
		// Set size so that bitdepths < 8 work correctly
		tex_2d(GL_RGBA, 256, 1, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	} else {
		level = WU_SCALING_POW;
		min = gl_min_linear;
		tex_2d_null();
	}
	tex_filter(GL_TEXTURE_2D, level, min, gl_mag_nearest);
	tex_active(gl_tex_img);
	tex_filter(GL_TEXTURE_2D, WU_SCALING_POW, min, gl_mag_nearest);
}

static void planar_disable(const enum gl_tex_unit start) {
	for (enum gl_tex_unit i = start; i <= gl_tex_plane_alpha; ++i) {
		tex_active(i);
		tex_2d_null();
	}
	tex_active(gl_tex_img);
}

static void switch_color_mode(struct gl_context *context,
enum image_mode new_mode) {
	if (new_mode != context->tex.mode) {
		switch (context->tex.mode) {
		case image_mode_raw:
		case image_mode_bitfield:
			break;
		case image_mode_palette:
			palette_parameters(false);
			break;
		case image_mode_planar:
			planar_disable((new_mode != image_mode_palette)
				? gl_tex_pal : gl_tex_plane3);
			break;
		}

		switch (new_mode) {
		case image_mode_raw:
		case image_mode_planar:
			break;
		case image_mode_palette:
			palette_parameters(true);
			break;
		case image_mode_bitfield:
			new_mode = image_mode_raw;
			break;
		}

		context->tex.mode = new_mode;
		glUniform1i(context->uni.mode.color, new_mode);
	}
}

static void gl_alignment(const align_t align) {
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1 << align);
}

static bool unpack_upload(const GLuint pix_buf,
const struct gl_upload_params *params, const struct wuimg *img, size_t w,
const size_t h, const unsigned char *data) {
	gl_alignment(DEFAULT_ALIGN);
	w *= params->comps;
	const void *arg = params->op == op_bitfield
		? (void *)img->u.bitfield : &img->bitrange;
	const size_t instride = strip_length(w, img->bitdepth, img->align_sh);
	const size_t outwidth = unpack_stride(w, img->bitdepth, img->attr,
		params->op, arg);
	if (!outwidth) {
		fatal_bug("Upload error", "Unsupported raster format");
	}
	const size_t outstride = strip_length(outwidth, 8, DEFAULT_ALIGN);

	uint8_t *map = map_unpack_buffer(pix_buf, outstride * h, GL_WRITE_ONLY);
	if (map) {
		const watch_t start = watch_look();
		for (size_t y = 0; y < h; ++y) {
			unpack_strip(map + outstride*y, data + instride*y,
				w, img->bitdepth, img->attr, params->op, arg);
		}
		watch_report("Unpacked", start, report_detail);
		glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER);
	}
	return (bool)map;
}

static bool tex_upload(struct gl_context *context, const struct wuimg *img,
const struct gl_upload_params *params, const size_t w, const size_t h,
const void *data) {
	const GLuint pix_buf = context->pixel_unpack_buf;
	bool bind_buffer = false;
	bool ok = true;
	if (params->op != op_noop || img->align_sh > 3) {
		ok = unpack_upload(pix_buf, params, img, w, h, data);
		data = 0;
		bind_buffer = true;
	} else {
		gl_alignment(img->align_sh);
	}

	if (ok) {
		tex_2d_params(params, w, h, data);
		if (bind_buffer) {
			glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
		}
	}

	const GLenum err = glGetError();
	if (err) {
		fprintf(stderr, "Encountered error %x in %s: %s\n",
			err, __func__, gl_strerror(err));
		return false;
	}
	return true;
}

static bool subtex_upload(struct gl_context *context, const struct wuimg *img,
const struct gl_upload_params *params, const struct compost *region) {
	gl_alignment(DEFAULT_ALIGN);
	size_t w = region->w * img->channels;
	size_t full_w = img->w * img->channels;

	const size_t instride = strip_length(full_w, img->bitdepth, img->align_sh);
	const size_t outstride = strip_length(w, img->bitdepth, DEFAULT_ALIGN);
	const size_t outlen = strip_base(w, img->bitdepth);

	size_t x_off = strip_base(region->x * img->channels, img->bitdepth);
	const uint8_t *data = img->data + x_off + instride * region->y;
	uint8_t *map = map_unpack_buffer(context->pixel_unpack_buf,
		outstride * region->h, GL_WRITE_ONLY);
	if (map) {
		for (size_t y = 0; y < region->h; ++y) {
			memcpy(map + outstride*y, data + instride*y, outlen);
		}
		glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER);
		tex_sub2d((GLint)region->x, (GLint)region->y, (GLsizei)region->w,
			(GLsizei)region->h, params->tex.fmt, params->tex.type, 0);
		glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
	}
	const GLenum err = glGetError();
	if (err) {
		fprintf(stderr, "Encountered error %x in %s: %s\n",
			err, __func__, gl_strerror(err));
	}
	return (bool)map;
}

static float get_coord_mul(const size_t dim, const size_t subdim,
const uint8_t subsamp) {
	if (subsamp > 1) {
		return (float)dim / (float)(subdim*subsamp);
	}
	return 1;
}

static void subsamp_positioning(float pos[static 4], const struct wuimg *img,
const struct plane_info *p) {
	/* Scale coordinates so that subsampled planes align with the full
	 * planes.
	 * Consider a 3x3 YCbCr 4:2:0 image, with the chroma planes measuring
	 * 2x2. If these were naively stretched, the pixel at Cb(0, 0) would
	 * span from Y(0, 0) to Y(1.5, 1.5), where the correct result should
	 * be to cover up to Y(2, 2), as on a 4x4 image. */
	pos[0] = get_coord_mul(img->w, p->w, p->x.subsamp);
	pos[1] = get_coord_mul(img->h, p->h, p->y.subsamp);
	pos[2] = -p->x.cosit / 2.0f / (float)p->w;
	pos[3] = -p->y.cosit / 2.0f / (float)p->h;
}

static bool planar_upload(struct gl_context *context,
const struct wuimg *img, const struct gl_upload_params *params) {
	uint8_t map[4];
	pix_layout_invert(map, img->layout);
	float pos[4][4] = {0};
	for (uint8_t i = 0; i < 4; ++i) {
		tex_active(map[i]);
		if (i < img->channels) {
			const struct plane_info *p = img->u.planes->p + i;
			tex_upload(context, img, params, p->w, p->h, p->ptr);
			subsamp_positioning(pos[i], img, p);
			context->tex.subsamp |=
				((p->x.subsamp > 1) | (p->y.subsamp > 1)) << i;
		} else {
			tex_2d_solid();
		}
		const GLenum err = glGetError();
		if (err) {
			fprintf(stderr, "Encountered error %x when uploading "
				"plane %d: %s\n", err, i, gl_strerror(err));
			return false;
		}
	}
	glUniform2fv(context->uni.positioning, ARRAY_LEN(pos)*2, *pos);
	tex_active(gl_tex_img);
	return true;
}

static bool mode_upload(struct gl_context *context, const struct wuimg *img,
const struct gl_upload_params *params, const struct compost *region) {
	switch (img->mode) {
	case image_mode_palette:
		tex_active(gl_tex_pal);
		tex_2d_swizzle(img->layout);
		tex_sub2d(0, 0, 1 << img->bitdepth, 1, GL_RGBA,
			GL_UNSIGNED_BYTE, img->u.palette->color);
		tex_active(gl_tex_img);
		// fallthrough
	case image_mode_raw:
		if (region) {
			if (!region->w || !region->h) {
				return true;
			}
			if (params->op == op_noop) {
				switch (img->bitdepth) {
				case 8: case 16: case 32:
					return subtex_upload(context, img,
						params, region);
				}
			}
		}
		// fallthrough
	case image_mode_bitfield:
		return tex_upload(context, img, params, img->w, img->h,
			img->data);
	case image_mode_planar:
		return planar_upload(context, img, params);
	}
	return false;
}

static GLint in_fmt_lut(const unsigned depth_log, const uint8_t ch) {
	const GLint lut[][4] = {
		{GL_R8, GL_RG8, GL_RGB8, GL_RGBA8},
		{GL_R16, GL_RG16, GL_RGB16, GL_RGBA16},
		{GL_R32F, GL_RG32F, GL_RGB32F, GL_RGBA32F},
	};
	return lut[depth_log][ch - 1];
}

static GLenum fmt_lut(const uint8_t ch) {
	switch (ch) {
	case 1: return GL_RED;
	case 2: return GL_RG;
	case 3: return GL_RGB;
	case 4: return GL_RGBA;
	}
	return 0;
}

static GLenum type_lut(const unsigned depth_log, const enum pix_attr attr) {
	switch (depth_log) {
	case 0: return GL_UNSIGNED_BYTE;
	case 1: return (attr == pix_float) ? GL_HALF_FLOAT : GL_UNSIGNED_SHORT;
	case 2: return (attr == pix_float) ? GL_FLOAT : GL_UNSIGNED_INT;
	}
	return 0;
}

static struct gl_tex_params get_tex_params(const uint8_t bd, const uint8_t ch,
const enum pix_attr attr) {
	const unsigned depth_log = bit_min_wordsize_log2(umin(bd, 32));
	return (struct gl_tex_params) {
		.in_fmt = in_fmt_lut(depth_log, ch),
		.fmt = fmt_lut(ch),
		.type = type_lut(depth_log, attr),
	};
}

static const char * set_upload_params(struct gl_upload_params *params,
const struct wuimg *img) {
	*params = (struct gl_upload_params){
		.op = op_noop,
		.bd = img->bitdepth,
	};
	switch (img->mode) {
	case image_mode_raw:
	case image_mode_bitfield:
		params->layout = img->layout;
		params->comps = img->channels;
		break;
	case image_mode_palette:
	case image_mode_planar:
		params->layout = pix_gray;
		params->comps = 1;
		break;
	}

	uint8_t ch = params->comps;
	uint8_t bd = img->bitdepth;
	struct gl_tex_params *tex = &params->tex;
	switch (img->attr) {
	case pix_normal:
		/* LM: least-to-most significant order, ML: most-to-least.
		 * OpenGL reads components in ML order within a machine word,
		 * unless the type ends with _REV, then it's LM. */
		if (ch == 4) {
			/* We use ML order for raw images. However, we don't
			 * care about machine words. */
			if (bd == 8) {
				tex->in_fmt = GL_RGBA8;
				tex->fmt = GL_BGRA;
				tex->type = GL_UNSIGNED_INT_8_8_8_8;
				/* In big-endian, the component order "matches"
				 * the memory layout, and so only swapping
				 * R and B is needed.
				 * Little-endian is just the reverse of
				 * big-endian, which surprisingly is not ARGB,
				 * but GBAR. */
				const enum pix_layout meta = which_end() == big_endian
					? pix_layout_pack(2, 1, 0, 3)
					: pix_layout_pack(3, 0, 1, 2);
				params->layout = pix_layout_mul(meta, params->layout);
				return NULL;
			} else if (bd == 4) {
				tex->in_fmt = GL_RGBA4;
				tex->fmt = GL_BGRA;
				tex->type = GL_UNSIGNED_SHORT_4_4_4_4_REV;
				/* fmt is BGRA, so swap R and B, then reverse
				 * for big-endian as the order is LM.
				 * Little-endian is big-endian but with the
				 * bytes (not nibbles!) swapped. */
				const enum pix_layout meta = which_end() == big_endian
					? pix_layout_pack(3, 0, 1, 2)
					: pix_layout_pack(1, 2, 3, 0);
				params->layout = pix_layout_mul(meta, params->layout);
				return NULL;
			}
		} else if (img->mode == image_mode_bitfield) {
			/* Bitfields are in LM order. */
			const struct bitfield *bf = img->u.bitfield;
			if (bf->id == 0x1555 || bf->id == 0x555) {
				tex->in_fmt = GL_RGB5_A1;
				tex->fmt = GL_BGRA;
				tex->type = GL_UNSIGNED_SHORT_1_5_5_5_REV;
				/* 1_5_5_5_REV is in LM order too, but as the
				 * format is GL_BGRA, swap R and B. */
				params->layout = pix_layout_mul(pix_bgra, params->layout);
				return NULL;
			} else if (bf->id == 0x332) {
				tex->in_fmt = GL_R3_G3_B2;
				tex->fmt = GL_RGB;
				tex->type = GL_UNSIGNED_BYTE_3_3_2;
				// Reverse the first three components.
				params->layout = pix_layout_mul(pix_bgra, params->layout);
				return NULL;
			}
			params->op = op_bitfield;
			bd = bf->outdepth;
			ch = bf->ch;
			break;
		}
		// fallthrough
	case pix_signed:
	case pix_inverted:
		switch (bd) {
		case 8: case 16: case 32:
			params->op = img->attr == pix_normal
				? op_noop : op_unpack;
			break;
		default:
			if (bd > 16) {
				params->op = op_pack;
				bd = 16;
			} else {
				params->op = op_unpack;
				bd = bd > 8 ? 16 : 8;
			}
		}
		break;
	case pix_float:
		switch (bd) {
		case 16: case 32:
			break;
		case 64:
			params->op = op_pack;
			break;
		default:
			return "Invalid floating-point depth";
		}
		break;
	default:
		return "Invalid pix attribute";
	}
	params->bd = bd;
	*tex = get_tex_params(bd, ch, img->attr);
	return NULL;
}

static void tex_cms(const size_t size) {
	const GLsizei s = (GLsizei)size;
	tex_active(gl_tex_cms);
	glTexImage3D(GL_TEXTURE_3D, 0, GL_RGB, s, s, s, 0, GL_RGB,
		GL_UNSIGNED_SHORT, 0);
	tex_active(gl_tex_img);
}

static bool set_icc_lut(const GLuint pix_buf, const struct color_space *cs,
cmsHPROFILE out) {
	const watch_t start = watch_look();
	cmsHTRANSFORM xfr = color_icc_transform(cs, out, icc_fmt(3, 1, 0),
		TYPE_RGB_16);
	if (!xfr) {
		return false;
	}

	const uint8_t bits = 5;
	const size_t size = 1 << bits;
	const size_t len = size*size*size;
	const size_t items = len*3;
	uint16_t *buf = map_unpack_buffer(pix_buf, items * sizeof(*buf),
		GL_READ_WRITE);
	uint8_t *in = (uint8_t *)buf + items;
	for (size_t b = 0; b < size; ++b) {
		for (size_t g = 0; g < size; ++g) {
			uint8_t *row = in + (b*size*size + g*size) * 3;
			for (size_t r = 0; r < size; ++r) {
				const uint8_t shl = 8 - bits;
				const uint8_t shr = bits - shl;
				row[r*3] = (uint8_t)((r << shl) + (r >> shr));
				row[r*3+1] = (uint8_t)((g << shl) + (g >> shr));
				row[r*3+2] = (uint8_t)((b << shl) + (b >> shr));
			}
		}
	}
	cmsDoTransform(xfr, in, buf, len);

	glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER);
	tex_cms(size);
	glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
	cmsDeleteTransform(xfr);
	watch_report("icc lut created", start, report_detail);
	return true;
}

static void set_cms(struct gl_context *context, const struct wuimg *img,
const struct gl_upload_params *params) {
	const struct color_space *cs = &img->cs;
	const struct gl_uni *uni = &context->uni;

	bool is_planar = false;
	double scale = 1;
	switch (img->mode) {
	case image_mode_planar:
		is_planar = true;
		// fallthrough
	case image_mode_raw:
		if (img->attr != pix_float && img->bitrange < params->bd) {
			scale = (exp2(params->bd) - 1) / (exp2(img->bitrange) - 1);
		}
		break;
	case image_mode_palette:
		scale = (exp2(8) - 1) / (exp2(img->bitrange) - 1);
		break;
	case image_mode_bitfield:
		break;
	}

	struct color_convert conv;
	const bool spacewalk = color_space_to_linear_sRGB(cs, &conv,
		img->layout == pix_gray, is_planar, scale);
	glUniform4fv(uni->remap, 2, conv.map.mul);
	glUniformMatrix3fv(uni->mat.nonlinear, 1, GL_FALSE, conv.nonlinear.m);

	enum gl_cms_mode {
		gl_cms_none,
		gl_cms_spacewalk,
		gl_cms_lut,
	} mode = gl_cms_none;
	if (cs->type == color_profile_icc) {
		if (!context->icc) {
			context->icc = color_icc_linear_sRGB();
		}
		if (context->icc
		&& set_icc_lut(context->pixel_unpack_buf, cs, context->icc)) {
			mode = gl_cms_lut;
		}
	}

	if (mode != gl_cms_lut) {
		tex_cms(0);
		glUniform1i(uni->eotf.fn, conv.eotf.fn);
		glUniform1fv(uni->eotf.args, ARRAY_LEN(conv.eotf.args),
			conv.eotf.args);
		if (spacewalk) {
			mode = gl_cms_spacewalk;
			glUniformMatrix3fv(uni->mat.cms, 1, GL_FALSE,
				conv.linear.m);
		}
	}
	glUniform1i(uni->mode.cms, mode);
}

static bool close_to_int(const float n) {
	float _int;
	const float frac = modff(n < 1.0f ? 1.0f/n : n, &_int);
	return frac < VISUAL_EPSILON || frac > (1.0 - VISUAL_EPSILON);
}

static float get_pixel_ratio(const struct wuimg *img, const enum heed_ratio heed) {
	switch (heed) {
	case heed_always: return img->ratio;
	case heed_pretty:
		if (img->mode != image_mode_palette || close_to_int(img->ratio)) {
			return img->ratio;
		}
		break;
	case heed_never: break;
	}
	return 1;
}

enum gl_upload_status gl_texture_upload(struct gl_context *context,
const struct wuimg *img, const enum heed_ratio heed) {
	if (img->channels > 4) {
		fprintf(stderr, "Number of color channels unsupported (%d given)\n",
			img->channels);
		return gl_upload_fail;
	}

	gl_clock_start(context);
	switch_color_mode(context, img->mode);

	struct gl_upload_params params;
	const char *errmsg = set_upload_params(&params, img);
	if (errmsg) {
		fatal_bug(__func__, errmsg);
		return gl_upload_fail;
	}

	set_cms(context, img, &params);

	context->tex.subsamp = 0;
	if (!mode_upload(context, img, &params, NULL)) {
		return gl_upload_fail;
	}
	gl_clock_end();

	context->tex.no_transform = img->scalable;
	context->tex.rotate = img->rotate;
	context->tex.mirror = img->mirror;
	context->tex.alpha = img->alpha;
	context->tex.shown_frame = img->frames ? img->frames->current : 0;
	context->tex.ratio = get_pixel_ratio(img, heed);
	context->update = gl_update_matrix;
	set_alpha_ops(context);
	if (context->tex.w != (float)img->w || context->tex.h != (float)img->h) {
		context->tex.w = (float)img->w;
		context->tex.h = (float)img->h;
		calc_fit_zoom(context);
		return gl_upload_success;
	}
	return gl_upload_same_size;
}

bool gl_subtexture_upload(struct gl_context *context, const struct wuimg *img,
const struct wu_state *state) {
	struct gl_upload_params params;
	const char *errmsg = set_upload_params(&params, img);
	if (errmsg) {
		fatal_bug(__func__, errmsg);
		return false;
	}

	const struct compost *region = NULL;
	if (img->frames && context->tex.shown_frame + 1 == state->frame) {
		region = &img->frames->f[state->frame].reg;
	}
	context->update = gl_update_redraw;
	context->tex.shown_frame = state->frame;
	return mode_upload(context, img, &params, region);
}

void gl_reader_read_row(struct gl_reader_context *reader, const size_t y,
void *restrict tgt) {
	reader->state.y_offset = (float)y;
	reader->context.update = gl_update_matrix;
	gl_draw(&reader->context, &reader->state);

	const struct wuimg *dst = reader->dst;
	const struct gl_tex_params tex = get_tex_params(dst->bitdepth,
		dst->channels, pix_normal);
	glReadPixels(0, 0, (GLsizei)dst->w, 1, tex.fmt, tex.type, tgt);
}

const char * gl_reader_set(struct gl_reader_context *reader,
const struct wuimg *dst, const struct wuimg *src) {
	reader->dst = dst;
	const enum gl_upload_status st = gl_texture_upload(&reader->context, src,
		heed_never);
	if (st == gl_upload_fail) {
		return "Failed to upload to texture";
	}

	tex_active(gl_tex_reader);
	const struct gl_tex_params tex = get_tex_params(dst->bitdepth,
		dst->channels, pix_normal);
	tex_2d(tex.in_fmt, (GLsizei)dst->w, 1, tex.fmt, tex.type, NULL);
	tex_active(gl_tex_img);

	reader->state = (struct wu_state) {
		.zoom = 1,
		.rotate = 0,
		.mirror = true,
	};
	struct display_dims dims = {
		.w = (int)dst->w,
		.h = (int)dst->h,
	};
	gl_viewport(&reader->context, &dims);
	return glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE
		? NULL : "GL framebuffer not complete";
}

static void enable_bind_tex(const GLint idx, const GLuint *texs,
const GLint *samps, const GLenum target) {
	tex_active((enum gl_tex_unit)idx);
	glBindTexture(target, texs[idx]);
	glUniform1i(samps[idx], idx);

	const GLint wrap = GL_CLAMP_TO_EDGE;
	glTexParameteri(target, GL_TEXTURE_WRAP_R, wrap);
	glTexParameteri(target, GL_TEXTURE_WRAP_S, wrap);
	glTexParameteri(target, GL_TEXTURE_WRAP_T, wrap);
}

static void setup_texture_cms(const GLint idx, const GLuint *texs,
const GLint *samps) {
	enable_bind_tex(idx, texs, samps, GL_TEXTURE_3D);
	tex_filter(GL_TEXTURE_3D, 0, gl_min_nearest, gl_mag_linear);
}

static void setup_texture2d(const GLint idx, const GLuint *texs,
const GLint *samps) {
	enable_bind_tex(idx, texs, samps, GL_TEXTURE_2D);
	tex_filter(GL_TEXTURE_2D, WU_SCALING_POW, gl_min_linear, gl_mag_nearest);
}

static bool get_uniforms(const GLuint program, GLint *uni,
const char **uni_names, const size_t len) {
	for (size_t i = 0; i < len; ++i) {
		uni[i] = glGetUniformLocation(program, uni_names[i]);
		if (uni[i] == -1) {
			return false;
		}
	}
	return true;
}

typedef void (*gl_infolog_func_t)(GLuint, GLsizei, GLsizei *, GLchar *);
typedef void (*gl_getiv_func_t)(GLuint, GLenum, GLint *);

static void print_gl_message(const GLchar *message, const GLsizei len) {
	if (len > 0) {
		fwrite(message, sizeof(*message), (size_t)len, stderr);
		if (message[len-1] != '\n') {
			fputc('\n', stderr);
		}
	}
}

static GLuint check_issues(const gl_infolog_func_t log, const gl_getiv_func_t iv,
const GLuint object, const GLenum parameter) {
	GLsizei written = 0;
	GLchar logbuf[512];
	(*log)(object, ARRAY_LEN(logbuf), &written, logbuf);
	print_gl_message(logbuf, written);

	GLint status = GL_FALSE;
	(*iv)(object, parameter, &status);
	return (status == GL_TRUE) ? object : 0;
}

static GLuint setup_program(const GLuint vshader, const GLuint fshader) {
	const GLuint program = glCreateProgram();
	glAttachShader(program, vshader);
	glAttachShader(program, fshader);
	glDeleteShader(vshader);
	glDeleteShader(fshader);
	glLinkProgram(program);
	return check_issues(glGetProgramInfoLog, glGetProgramiv, program,
		GL_LINK_STATUS);
}

static GLuint setup_shader(const char *shader_code, const GLenum type) {
	const GLuint shader = glCreateShader(type);
	glShaderSource(shader, 1, &shader_code, NULL);
	glCompileShader(shader);
	return check_issues(glGetShaderInfoLog, glGetShaderiv, shader,
		GL_COMPILE_STATUS);
}

static void debug_print(GLenum source, GLenum type, GLuint id, GLenum severity,
GLsizei len, const GLchar *message, const void *user_data) {
	(void)source;
	(void)type;
	(void)id;
	(void)severity;
	(void)user_data;
	print_gl_message(message, len);
}

bool gl_context_setup(struct gl_context *context, struct wu_conf *wuconf) {
	const bool is_debug = getenv("WU_DEBUG");
	if (is_debug) {
		fprintf(stderr, "vendor: %s\n"
			"renderer: %s\n"
			"version: %s\n"
			"shading: %s\n",
			glGetString(GL_VENDOR), glGetString(GL_RENDERER),
			glGetString(GL_VERSION), glGetString(GL_SHADING_LANGUAGE_VERSION));
		glDebugMessageCallback(debug_print, NULL);
		glEnable(GL_DEBUG_OUTPUT);
	}

	const char vs[] =
		"#version 330 core\n"
		"in vec2 " ATTR_POS ";"
		"out vec2 texcoord;"
		"uniform mat3 " UNI_MAT_POS ";"
		"void main() {"
			"texcoord =" ATTR_POS " * vec2(.5) + vec2(.5);"
			"gl_Position = vec4(" ATTR_POS ", 0.0, 1.0);"
			"gl_Position.xyw =" UNI_MAT_POS " * gl_Position.xyw;"
		"}";
	const char fs[] =
		"#version 330 core\n"
		"in vec2 texcoord;"
		"out vec4 color;"

		"uniform sampler2D " UNI_IMG ";"
		"uniform sampler2D " UNI_PAL ";"
		"uniform sampler2D " UNI_PLANE3 ";"
		"uniform sampler2D " UNI_PLANE_ALPHA ";"
		"uniform sampler3D " UNI_CMS_LUT ";"

		"uniform mat3 " UNI_MAT_NONLINEAR ";"
		"uniform mat3 " UNI_MAT_CMS ";"
		"uniform int " UNI_MODE_COLOR ";"
		"uniform int " UNI_MODE_CMS ";"
		"uniform int[2] " UNI_MODE_ALPHA ";"
		"uniform int " UNI_EOTF_FN ";"
		"uniform float[5] " UNI_EOTF_ARGS ";"
		"uniform vec2[8] " UNI_POSITIONING ";"
		"uniform vec4[2] " UNI_REMAP ";"

		"void gen_check_pattern() {"
			"ivec2 d = ivec2(gl_FragCoord.xy);"
			"float bg = bool((d.x ^ d.y) & 16) ? .75 : .5;"
			"color.rgb += vec3(bg - bg * color.a);"
		"}"

		"float srgb_oetf(float c) {"
			"if (c > 0.0031308) {"
				"return pow(c, 1.0/2.4) * 1.055 - 0.055;"
			"}"
			"return c * 12.92;"
		"}"

		"float setsign(float x, float y) {"
			"return y >= 0.0 ? x : -x;"
		"}"
		"float linear_gamma(float c, float[5] arg) {"
			"if (c > arg[0]) {"
				"return pow(c * arg[1] + arg[2], arg[3]);"
			"}"
			"return c * arg[4];"
		"}"
		"vec3 perceptual_quantization(vec3 c, float[5] arg) {"
			"c = pow(c, vec3(arg[0]));"
			"vec3 num = max(c - vec3(arg[1]), vec3(0.0));"
			"vec3 den = vec3(arg[2]) - vec3(arg[3]) * c;"
			"return pow(num / den, vec3(arg[4]));"
		"}"
		"float hybrid_log_gamma(float c, float[5] arg) {"
			"if (c > arg[0]) {"
				"return exp2(c * arg[1] + arg[2]) + arg[3];"
			"}"
			"return c * c * arg[4];"
		"}"
		"vec3 eotf(vec3 c) {"
			"switch (" UNI_EOTF_FN ") {"
			"case " EOTF_LINEAR_GAMMA ":"
				"for (int i = 0; i < 3; ++i) {"
					"c[i] = setsign("
						"linear_gamma(abs(c[i])," UNI_EOTF_ARGS "),"
						"c[i]);"
				"}"
				"break;"
			"case " EOTF_PQ ":"
				"c = perceptual_quantization(c," UNI_EOTF_ARGS ");"
				"break;"
			"case " EOTF_HLG ":"
				"for (int i = 0; i < 3; ++i) {"
					"c[i] = hybrid_log_gamma(c[i]," UNI_EOTF_ARGS ");"
				"}"
				"break;"
			"}"
			"return c;"
		"}"
		"vec2 subsamp_coord(int i) {"
			"return texcoord *" UNI_POSITIONING "[i*2] +" UNI_POSITIONING "[i*2+1];"
		"}"
		"void main() {"
			"if (" UNI_MODE_COLOR "==" COLOR_PLANAR ") {"
				"color = vec4("
					"texture(" UNI_IMG ", subsamp_coord(0)).r,"
					"texture(" UNI_PAL ", subsamp_coord(1)).r,"
					"texture(" UNI_PLANE3 ", subsamp_coord(2)).r,"
					"texture(" UNI_PLANE_ALPHA ", subsamp_coord(3)).r"
				");"
			"} else {"
				"color = texture(" UNI_IMG ", texcoord);"
				"if (" UNI_MODE_COLOR "==" COLOR_PALETTE ") {"
					"color = texture(" UNI_PAL ","
						"vec2(color.r, 0.0));"
				"}"
			"}"

			"color = color *" UNI_REMAP "[0] +" UNI_REMAP "[1];"
			"color.rgb =" UNI_MAT_NONLINEAR "* color.rgb;"
			"if (" UNI_MODE_CMS "==" CMS_LUT ") {"
				"color.rgb = texture("
					UNI_CMS_LUT ", color.rgb).rgb;"
			"} else {"
				"color.rgb = eotf(color.rgb);"
				"if (" UNI_MODE_CMS "==" CMS_SPACEWALK ") {"
					"color.rgb *=" UNI_MAT_CMS ";"
				"}"
			"}"

			"switch (" UNI_MODE_ALPHA "[0]) {"
			"case " ALPHA_COLOR_MULTIPLY ": color.rgb *= color.aaa; break;"
			"case " ALPHA_COLOR_NO_MULTIPLY ": break;"
			"case " ALPHA_COLOR_UNMULTIPLY ":"
				"if (color.a != 0.0) {"
					"color.rgb /= color.aaa; break;"
				"}"
				"break;"
			"}"
			"switch (" UNI_MODE_ALPHA "[1]) {"
			"case " ALPHA_BG_CHECKERS ": gen_check_pattern();" // fallthrough
			"case " ALPHA_BG_ONE ": color.a = 1.0;"
			"}"

			"for (int i = 0; i < 3; ++i) {"
				"color[i] = srgb_oetf(color[i]);"
			"}"
		"}";
	const GLuint vshader = setup_shader(vs, GL_VERTEX_SHADER);
	if (!vshader) {
		return false;
	}
	const GLuint fshader = setup_shader(fs, GL_FRAGMENT_SHADER);
	if (!fshader) {
		return false;
	}
	const GLuint program = setup_program(vshader, fshader);
	if (!program) {
		return false;
	}
	glUseProgram(program);
	glDeleteProgram(program);


	const char *samps_name[] = {
		UNI_IMG, UNI_PAL, UNI_PLANE3, UNI_PLANE_ALPHA, UNI_CMS_LUT
	};
	const char *uni_name[] = {
		UNI_MAT_POS,
		UNI_MAT_NONLINEAR,
		UNI_MAT_CMS,
		UNI_MODE_COLOR,
		UNI_MODE_ALPHA,
		UNI_MODE_CMS,
		UNI_EOTF_FN,
		UNI_EOTF_ARGS,
		UNI_POSITIONING,
		UNI_REMAP,
	};
	GLint samps[ARRAY_LEN(samps_name)];
	GLint *uni = (GLint *)&context->uni;
	if (!get_uniforms(program, samps, samps_name, ARRAY_LEN(samps_name))
	|| !get_uniforms(program, uni, uni_name, ARRAY_LEN(uni_name))) {
		return false;
	}


	GLuint texs[ARRAY_LEN(samps)];
	glGenTextures(ARRAY_LEN(texs), texs);

	GLint s = (GLint)ARRAY_LEN(samps) - 1;
	setup_texture_cms(s, texs, samps);
	while (s > 0) {
		--s;
		setup_texture2d(s, texs, samps);
	}


	GLuint vertex_array;
	glGenVertexArrays(1, &vertex_array);
	glBindVertexArray(vertex_array);


	GLuint array_buf[2];
	glGenBuffers(ARRAY_LEN(array_buf), array_buf);
	context->pixel_unpack_buf = array_buf[0];
	const GLint attr_loc = glGetAttribLocation(program, ATTR_POS);
	if (attr_loc < 0) {
		return false;
	}
	const GLbyte vertices[] = {
		-1,-1,  1,-1,
		-1, 1,  1, 1,
	};
	bind_buffer_data(GL_ARRAY_BUFFER, array_buf[1], sizeof(vertices),
		vertices, GL_STATIC_DRAW);
	glVertexAttribPointer((GLuint)attr_loc, 2, GL_BYTE, GL_FALSE, 0, 0);
	glEnableVertexAttribArray((GLuint)attr_loc);


	glHint(GL_FRAGMENT_SHADER_DERIVATIVE_HINT, GL_FASTEST);
	glDisable(GL_POLYGON_SMOOTH);
	glEnable(GL_BLEND);
	glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
	if (wuconf->bg_src == bg_default) {
		gl_clear_color(wuconf->bg);
	}

	GLuint mts;
	glGetIntegerv(GL_MAX_TEXTURE_SIZE, (GLint *)&mts);
	if (is_debug) {
		fprintf(stderr, "Texture size limit: %u\n", mts);
	}
	if (wuconf->max_img_size) {
		wuconf->max_img_size = umin(wuconf->max_img_size, mts);
	} else {
		wuconf->max_img_size = mts;
	}

	glGenQueries(1, &context->timer);
	return true;
}

bool gl_reader_init(struct gl_reader_context *reader, struct wu_conf *wuconf) {
	if (gl_context_setup(&reader->context, wuconf)) {
		GLuint framebuffer;
		glGenFramebuffers(1, &framebuffer);
		glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);

		GLuint tex;
		glGenTextures(1, &tex);
		tex_active(gl_tex_reader);
		glBindTexture(GL_TEXTURE_2D, tex);
		tex_filter(GL_TEXTURE_2D, 0, gl_min_nearest, gl_mag_nearest);

		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
			GL_TEXTURE_2D, tex, 0);
		tex_active(gl_tex_img);

		glDisable(GL_BLEND);
		reader->context.unmultiply = true;
		return true;
	}
	return false;
}

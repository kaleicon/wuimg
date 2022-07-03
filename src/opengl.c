#include <stdio.h>
#include <math.h>
#include <limits.h>
#include <string.h>

#include <epoxy/gl.h>

#include "opengl.h"
#include "raster/color.h"
#include "raster/pix.h"
#include "raster/unpack.h"

//#define WU_GL_DEBUG

/* GLSL variables */
#define ATTR_POS "pos"

#define UNI_IMG "img"
#define UNI_PAL "pal"
#define UNI_PLANE3 "plane3"
#define UNI_PLANE4 "plane4"
#define UNI_CMS_LUT "cms_lut"

#define UNI_POS_MATRIX "pos_matrix"
#define UNI_COLOR_MODE "color_mode"
#define UNI_ALPHA_OP "alpha_op"
#define UNI_CMS_MODE "cms_mode"
#define UNI_PLANE_OFFSETS "plane_offsets"
#define UNI_TO_RGBA "to_rgba"
#define UNI_CMS_MAT "cms_mat"
#define UNI_TRANSFER "transfer"
#define UNI_ARGS "args"

#define COLOR_RAW "0"
#define COLOR_PALETTE "1"
#define COLOR_PLANAR "2"

#define CMS_NONE "0"
#define CMS_SPACEWALK "1"
#define CMS_LUT "2"

#define TRANSFER_LINEAR_GAMMA "0"
#define TRANSFER_LOG "1"
#define TRANSFER_PQ "2"
#define TRANSFER_HLG "3"

static const GLint WU_MIPMAP_MAX = 6;

enum gl_mag_filter {
	gl_mag_best = GL_LINEAR,
	gl_mag_fast = GL_NEAREST,
};

enum gl_min_filter {
//	gl_min_linear = GL_LINEAR,
	gl_min_linear = GL_LINEAR_MIPMAP_LINEAR,
	gl_min_nearest = GL_NEAREST,
};

enum gl_tex_unit {
	gl_tex_img,
	gl_tex_pal,
	gl_tex_plane3,
	gl_tex_plane4,
	gl_tex_cms,
	gl_tex_reader,
};

struct gl_upload_params {
	GLint in_fmt;
	GLenum fmt, type;
	enum pix_layout layout:8;
	enum unpack_op op:8;
	uint8_t comps;
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

GLuint64 gl_clock_query(const struct gl_context *context) {
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
	const int alpha = (context->user_alpha + context->alpha) % 6;
	const GLint ops[] = {
		!(alpha & alpha_no_multiply),
		alpha >> 1,
	};
	glUniform1iv(context->uni.alpha_op, ARRAY_LEN(ops), ops);
}

void gl_alpha_toggle(struct gl_context *context, const int cycle) {
	context->user_alpha = (uint8_t)imod(context->user_alpha + cycle, 6);
	context->update_matrix = true;
	set_alpha_ops(context);
}

static float fix_aspect_ratio(GLfloat *mat, const struct gl_context *context,
const int rotate) {
	// Scale the image to its natural size, taking rotation into account.
	const int r1 = rotate & 1;
	const float ratio_w = context->tex.w * context->fb_wh[r1];
	const float ratio_h = context->tex.h * context->fb_wh[r1^1];

	if (mat) {
		const int r2 = 5 - r1;
		mat[r1] *= ratio_w;
		mat[r2] *= ratio_h;
	}
	return 1 / fmaxf(ratio_w, ratio_h);
}

static int bool_to_sign(const bool val) {
	return val ? 1 : -1;
}

static GLfloat hard_math(const int rotate) {
	return (GLfloat)( (rotate & 1) * bool_to_sign(rotate & 2) );
}

static void set_mirrot(GLfloat *mat, const int rotate, const bool mirror) {
	// Do some hard math to apply both rotation and mirroring
	const GLfloat cosy = hard_math(rotate - 1);
	const GLfloat sinner = hard_math(rotate);
	const GLfloat mirror_mult = (GLfloat)bool_to_sign(mirror);

	/* GL textures are bottom-up. We switch the signs of mirror_mult to
	 * flip to top-down without anyone knowing. */
	mat[0] = cosy;
	mat[1] = sinner;
	mat[4] = sinner * -mirror_mult;
	mat[5] = cosy * mirror_mult;
}

void gl_matrix_update(struct gl_context *context, struct wu_state *state) {
	GLfloat mat[16] = {
		1, 0, 0, 0,
		0, 1, 0, 0,
		0, 0, 1, 0,
		0, 0, 0, 1,
	};

	const int rot = context->tex.rotate + state->rotate;
	set_mirrot(mat, rot, context->tex.mirror ^ state->mirror);
	state->fit_zoom = fix_aspect_ratio(mat, context, rot);

	/* We receive input measured in pixels from the top left corner, which
	 * is sort of like the interval 0..1, but GL renders from the center
	 * between -1..1, so we scale offsets by 2 to keep things working. */
	const float scale = 2;
	/* Using exact integer offsets causes ugly artifacts when rendering.
	 * We add a fraction of a pixel to remedy this. */
	const float fix = 1.0f / 17.0f;
	mat[12] += (floorf( state->x_offset*scale) + fix) * context->fb_wh[0];
	mat[13] += (floorf(-state->y_offset*scale) + fix) * context->fb_wh[1];
	mat[15] = 1 / state->zoom;
	glUniformMatrix4fv(context->uni.pos_matrix, 1, GL_FALSE, mat);

	context->update_matrix = false;
}

float gl_fit_zoom(const struct gl_context *context, const uint8_t rotation) {
	return fix_aspect_ratio(NULL, context, rotation);
}

void gl_viewport(struct gl_context *context, const struct display_dims *dims) {
	context->fb_wh[0] = 1.0f/(float)dims->w;
	context->fb_wh[1] = 1.0f/(float)dims->h;
	context->update_matrix = true;
	glViewport(0, 0, (int)dims->w, (int)dims->h);
}

static void tex_active(const enum gl_tex_unit unit) {
	glActiveTexture(GL_TEXTURE0 + unit);
}

static void tex_filter(const GLenum target, const GLint level,
const enum gl_min_filter min, const enum gl_mag_filter mag) {
	glTexParameteri(target, GL_TEXTURE_MAX_LEVEL, level);
	glTexParameteri(target, GL_TEXTURE_MIN_FILTER, min);
	glTexParameteri(target, GL_TEXTURE_MAG_FILTER, mag);
}

static void tex_2d_parameteri(const GLenum name, const GLint param) {
	glTexParameteri(GL_TEXTURE_2D, name, param);
}

static void tex_2d_swizzle(const enum pix_layout layout) {
	GLint swz[pix_color_total] = {GL_RED, GL_GREEN, GL_BLUE, GL_ALPHA};
	pix_layout_swizzle(swz, pix_color_total, sizeof(*swz), layout);
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
	tex_2d(p->in_fmt, (GLsizei)w, (GLsizei)h, p->fmt, p->type, data);
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

static enum pix_layout layout_equiv(enum pix_layout layout,
const bool bgra_swap, const bool reverse) {
	/* On some cards GL_BGRA seems to be required for good texture upload
	 * performance, moreso for packed formats. We modify the layout to
	 * upload with good parameters then fix the colors by swizzling. */
	enum pix_layout out = 0;
	for (enum pix_color i = 0; i < pix_color_total; ++i) {
		unsigned sh = i*2;
		unsigned off = (layout >> sh) & 0x3;
		if (reverse) {
			sh = 6 - sh;
		}
		if (bgra_swap && (off + reverse) % 2 == 0) {
			off ^= 2;
		}
		out |= off << sh;
	}
	return out;
}

static void palette_parameters(const bool enable) {
	tex_active(gl_tex_pal);
	GLint level;
	enum gl_min_filter min;
	if (enable) {
		level = 0;
		min = gl_min_nearest;
		tex_2d(GL_RGBA, 256, 1, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	} else {
		level = WU_MIPMAP_MAX;
		min = gl_min_linear;
		tex_2d_null();
	}
	tex_2d_parameteri(GL_TEXTURE_MAX_LEVEL, level);
	tex_2d_parameteri(GL_TEXTURE_MIN_FILTER, min);
	tex_active(gl_tex_img);
	tex_2d_parameteri(GL_TEXTURE_MIN_FILTER, min);
}

static void planar_disable(const enum gl_tex_unit start) {
	for (enum gl_tex_unit i = start; i <= gl_tex_plane4; ++i) {
		tex_active(i);
		tex_2d_null();
	}
	tex_active(gl_tex_img);
}

static void switch_color_mode(struct gl_context *context,
const enum image_mode new_mode) {
	if (new_mode != context->mode) {
		switch (context->mode) {
		case image_mode_raw: break;
		case image_mode_palette:
			palette_parameters(false);
			break;
		case image_mode_planar:
			planar_disable((new_mode != image_mode_palette)
				? gl_tex_pal : gl_tex_plane3);
			break;
		}

		if (new_mode == image_mode_palette) {
			palette_parameters(true);
		}

		context->mode = new_mode;
		glUniform1i(context->uni.color_mode, new_mode);
	}
}

static size_t calc_map_outstride(const struct raw_img *img, const size_t w,
const enum unpack_op op, const uint8_t alignment) {
	glPixelStorei(GL_UNPACK_ALIGNMENT, alignment);
	if (op == op_noop) {
		return scanline_length(w, img->bitdepth, alignment);
	}
	const size_t outstride = unpack_stride(w, img->bitdepth, img->attr, op);
	if (!outstride) {
		fatal_bug("Upload failure", "Unsupported raster format");
	}
	return scanline_length(outstride, 8, alignment);
}

static void * unpack_upload(const GLuint pix_buf, const enum unpack_op op,
const struct raw_img *img, const size_t w, const size_t h,
const unsigned char *data) {
	const size_t instride = scanline_length(w, img->bitdepth, img->alignment);
	const size_t outstride = calc_map_outstride(img, w, op, 4);

	unsigned char *map = map_unpack_buffer(pix_buf, outstride * h,
		GL_READ_ONLY);
	const clock_t start = clock();
	for (size_t y = 0; y < h; ++y) {
		unpack_or_copy_strip(map + outstride*y, data + instride*y,
			w, img->bitdepth, img->attr, op);
	}
	printf("unpacked in %f\n", clock_ellapsed(start));
	glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER);
	return 0;
}

static bool tex_upload(struct gl_context *context, const struct raw_img *img,
const struct gl_upload_params *params, const size_t w, const size_t h,
const void *data) {
	const GLuint pix_buf = context->pixel_unpack_buf;
	bool bind_buffer = false;
	if (params->op != op_noop || img->attr == pix_inverted || img->alignment > 8) {
		const size_t elems = w * params->comps;
		data = unpack_upload(pix_buf, params->op, img, elems, h, data);
		bind_buffer = true;
	} else {
		glPixelStorei(GL_UNPACK_ALIGNMENT, img->alignment);
	}

	tex_2d_params(params, w, h, data);
	if (bind_buffer) {
		glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
	}

	const GLenum err = glGetError();
	if (err) {
		fprintf(stderr, "Encountered error %x when uploading to "
			"texture: %s\n", err, gl_strerror(err));
		return false;
	}
	return true;
}

static bool planar_upload(struct gl_context *context,
const struct raw_img *img, const struct gl_upload_params *params) {
	for (uint8_t i = 0; i < 4; ++i) {
		const struct plane_info *p = img->u.planes->p;
		tex_active(i);
		if (i < img->channels) {
			tex_upload(context, img, params, p[i].w, p[i].h,
				p[i].ptr);
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
	tex_active(gl_tex_img);
	return true;
}

static bool mode_upload(struct gl_context *context, const struct raw_img *img,
const struct gl_upload_params *params) {
	switch (img->mode) {
	case image_mode_palette:
		tex_active(gl_tex_pal);
		tex_2d_swizzle(img->layout);
		tex_sub2d(0, 0, 1 << img->bitdepth, 1, GL_RGBA,
			GL_UNSIGNED_BYTE, img->u.palette);
		tex_active(gl_tex_img);
		// fallthrough
	case image_mode_raw:
		return tex_upload(context, img, params, img->w, img->h, img->data);
	case image_mode_planar:
		return planar_upload(context, img, params);
	}
	return false;
}

static GLenum type_lut(const int depth_log, const enum pix_attr attr) {
	switch (depth_log) {
	case 0: switch (attr) {
		case pix_normal: case pix_inverted: return GL_UNSIGNED_BYTE;
		case pix_signed: return GL_BYTE;
		default: break;
		}
		break;
	case 1: switch (attr) {
		case pix_normal: case pix_inverted: return GL_UNSIGNED_SHORT;
		case pix_signed: return GL_SHORT;
		case pix_float: return GL_HALF_FLOAT;
		default: break;
		}
		break;
	case 2: switch (attr) {
		case pix_normal: case pix_inverted: return GL_UNSIGNED_INT;
		case pix_signed: return GL_INT;
		case pix_float: return GL_FLOAT;
		default: break;
		}
	}
	return 0;
}

static const char * set_upload_params(struct gl_upload_params *params,
const struct raw_img *img) {
	const uint8_t ch = params->comps;
	uint8_t bd = img->bitdepth;
	switch (img->attr) {
	case pix_packing_332:
		params->in_fmt = GL_R3_G3_B2;
		params->fmt = GL_RGB;
		params->type = GL_UNSIGNED_BYTE_3_3_2;
		return NULL;
	case pix_packing_1555:
		params->in_fmt = GL_RGB5_A1;
		params->fmt = GL_BGRA;
		params->type = GL_UNSIGNED_SHORT_1_5_5_5_REV;
		params->layout = layout_equiv(params->layout, true, false);
		return NULL;
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
	case pix_normal:
		if (bd == 4 && ch == 4) {
			params->in_fmt = GL_RGBA4;
			params->fmt = GL_BGRA;
			params->type = GL_UNSIGNED_SHORT_4_4_4_4_REV;
			params->layout = layout_equiv(params->layout,
				true, true);
			return NULL;
		}
		// fallthrough
	case pix_signed:
	case pix_inverted:
		switch (bd) {
		case 8: case 16: case 32:
			break;
		default:
			if (bd > 16) {
				params->op = op_pack;
				bd = 16;
			} else if (img->mode == image_mode_palette) {
				params->op = op_unpack;
			} else {
				params->op = op_expand;
			}
		}
		break;
	default:
		return "Invalid pix attribute";
	}

	const GLint in_fmt_lut[][4] = {
		{GL_R8, GL_RG8, GL_RGB8, GL_RGBA8},
		{GL_R16, GL_RG16, GL_RGB16, GL_RGBA16},
		{GL_R32F, GL_RG32F, GL_RGB32F, GL_RGBA32F},
	};
	const GLenum fmt_lut[] = {GL_RED, GL_RG, GL_BGR, GL_BGRA};

	const int depth = imin(ilog2(bd+7) - 3, 2);
	params->in_fmt = in_fmt_lut[depth][ch - 1];
	params->fmt = fmt_lut[ch - 1];
	params->type = type_lut(depth, img->attr);
	if (ch >= 3) {
		params->layout = layout_equiv(params->layout, true, false);
	}
	return NULL;
}

static void tex_cms(const size_t size) {
	const GLsizei s = (GLsizei)size;
	tex_active(gl_tex_cms);
	glTexImage3D(GL_TEXTURE_3D, 0, GL_RGB, s, s, s, 0, GL_RGB,
		GL_UNSIGNED_SHORT, 0);
	tex_active(gl_tex_img);
}

static bool set_icc_lut(const GLuint pix_buf, struct color_space *cs,
cmsHPROFILE out) {
	const clock_t start = clock();
	cmsHTRANSFORM xfr = color_icc_transform(cs, out);
	if (!xfr) {
		return false;
	}

	const size_t size = 64;
	const size_t len = size*size*size;
	const size_t items = len*3;
	uint16_t *buf = map_unpack_buffer(pix_buf, items * sizeof(*buf),
		GL_READ_WRITE);
	uint8_t *in = (uint8_t *)buf + items;
	for (size_t b = 0; b < size; ++b) {
		for (size_t g = 0; g < size; ++g) {
			uint8_t *row = in + (b*size*size + g*size) * 3;
			for (size_t r = 0; r < size; ++r) {
				row[r*3] = (uint8_t)((r << 2) + (r >> 4));
				row[r*3+1] = (uint8_t)((g << 2) + (g >> 4));
				row[r*3+2] = (uint8_t)((b << 2) + (b >> 4));
			}
		}
	}
	cmsDoTransform(xfr, in, buf, len);

	glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER);
	tex_cms(size);
	glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
	printf("created in %f\n", clock_ellapsed(start));
	return true;
}

static void set_cms(struct gl_context *context, struct raw_img *img) {
	struct color_space *cs = &img->cs;
	const struct gl_uni *uni = &context->uni;
	if (img->mode == image_mode_planar) {
		struct color_mat cm;
		color_mat_gen(&cm, cs, img->layout);

		glUniform4fv(uni->plane_offsets, 1, cm.off);
		glUniformMatrix4fv(uni->to_rgba, 1, GL_FALSE, cm.mat);
	}

	enum gl_cms_mode {
		gl_cms_none,
		gl_cms_spacewalk,
		gl_cms_lut,
	} mode = gl_cms_none;
	if (cs->type == color_profile_icc) {
		if (!context->icc) {
			context->icc = color_icc_linear_sRGB();
		}
		if (context->icc) {
			if (set_icc_lut(context->pixel_unpack_buf, cs,
			context->icc)) {
				mode = gl_cms_lut;
			}
		}
	}

	if (mode != gl_cms_lut) {
		tex_cms(0);
		struct color_convert conv;
		if (color_space_to_linear_sRGB(cs, &conv)) {
			glUniformMatrix3fv(uni->cms_mat, 1, GL_FALSE, conv.mat);
			mode = gl_cms_spacewalk;
		}
		glUniform1i(uni->transfer, conv.eotf);
		glUniform1fv(uni->args, ARRAY_LEN(conv.args), conv.args);
	}
	glUniform1i(uni->cms_mode, mode);
}

enum gl_upload_status gl_texture_upload(struct gl_context *context,
struct raw_img *img) {
	if (img->channels > 4) {
		printf("Number of color channels unsupported (%d given)\n",
			img->channels);
		return gl_upload_fail;
	}

	gl_clock_start(context);
	set_cms(context, img);

	struct gl_upload_params params = {.op = op_noop};
	switch_color_mode(context, img->mode);
	if (img->mode == image_mode_raw) {
		params.layout = img->layout;
		params.comps = img->channels;
	} else {
		params.layout = pix_gray;
		params.comps = 1;
	}

	const char *errmsg = set_upload_params(&params, img);
	if (errmsg) {
		puts(errmsg);
		return gl_upload_fail;
	}

	if (!mode_upload(context, img, &params)) {
		return gl_upload_fail;
	}

	context->alpha = img->alpha;
	set_alpha_ops(context);

	gl_clock_end();
	context->tex.rotate = img->rotate;
	context->tex.mirror = img->mirror;
	if (context->tex.w != (int)img->w || context->tex.h != (int)img->h) {
		context->tex.w = (float)img->w;
		context->tex.h = (float)img->h;
		return gl_upload_success;
	}
	return gl_upload_same_size;
}

void gl_draw(const struct gl_context *context) {
	gl_clock_start(context);
	glClear(GL_COLOR_BUFFER_BIT);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	gl_clock_end();
}

void gl_clear_color(const uint8_t bg[static 4]) {
	const float scale = 1.0f / UCHAR_MAX;
	const float alpha = bg[3] * scale;
	float sRGB[3];
	for (size_t i = 0; i < ARRAY_LEN(sRGB); ++i) {
		sRGB[i] = powf(bg[i] * scale, 0.454545f) * alpha;
	}
	glClearColor(sRGB[0], sRGB[1], sRGB[2], alpha);
}

void gl_reader_read_row(struct gl_context *context, struct wu_state *state,
const struct gl_reader *reader, void *restrict dst, const size_t row) {
	state->y_offset = (float)row;
	gl_matrix_update(context, state);
	gl_draw(context);
	glReadPixels(0, 0, (GLsizei)reader->w, 1, reader->fmt, reader->type,
		dst);
}

static unsigned char get_render_channels(const struct raw_img *img) {
	unsigned char ch;
	if (img->mode == image_mode_palette) {
		ch = 4;
	} else {
		switch (img->attr) {
		case pix_packing_332: ch = 3; break;
		case pix_packing_1555: ch = 4; break;
		default: ch = img->channels; break;
		}
	}
	if (img->alpha & alpha_one
	&& pix_layout_offset(img->layout, pix_alpha) < ch) {
		ch = (unsigned char)(ch - 1);
	}
	return ch;
}

bool gl_reader_set(struct gl_context *context, struct wu_state *state,
struct gl_reader *r, const struct raw_img *img) {
	const GLint in_fmts[][4] = {
		{GL_R8, GL_RG8, GL_RGB8, GL_RGBA8},
		{GL_R16, GL_RG16, GL_RGB16, GL_RGBA16},
	};
	const GLenum fmts[] = {GL_RED, GL_RG, GL_RGB, GL_RGBA};

	const bool swap = img->rotate & 1;
	r->w = (swap) ? img->h : img->w;
	r->h = (swap) ? img->w : img->h;
	r->ch = get_render_channels(img);
	r->bd = (img->bitdepth > 8) ? 16 : 8;
	r->fmt = fmts[r->ch - 1];
	r->type = (img->bitdepth > 8) ? GL_UNSIGNED_SHORT : GL_UNSIGNED_BYTE;
	r->len = r->w * r->ch * (r->bd / 8);

	const GLint in_fmt = in_fmts[img->bitdepth > 8][r->ch - 1];

	tex_active(gl_tex_reader);
	tex_2d(in_fmt, (GLsizei)r->w, 1, r->fmt, r->type, NULL);
	tex_active(gl_tex_img);

	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
		return false;
	}

	state->zoom = 1;
	state->rotate = img->rotate;
	state->mirror = !img->mirror; /* At this point I don't know why this
		extra flip is needed. */

	struct display_dims dims = {
		.w = (int)r->w,
		.h = (int)r->h,
	};
	gl_viewport(context, &dims);

	if (img->layout == pix_gray) {
		tex_2d_swizzle(pix_rgba);
	}
	return true;
}

void gl_reader_unbind(void) {
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void gl_reader_bind(struct gl_context *context) {
	if (context->framebuffer) {
		glBindFramebuffer(GL_FRAMEBUFFER, context->framebuffer);
	} else {
		glGenFramebuffers(1, &context->framebuffer);
		glBindFramebuffer(GL_FRAMEBUFFER, context->framebuffer);

		GLuint tex;
		glGenTextures(1, &tex);
		tex_active(gl_tex_reader);
		glBindTexture(GL_TEXTURE_2D, tex);
		tex_filter(GL_TEXTURE_2D, 0, gl_min_nearest, gl_mag_fast);

		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
			GL_TEXTURE_2D, tex, 0);
		tex_active(gl_tex_img);
	}
}

static void enable_bind_tex(const GLint idx, const GLuint *texs,
const GLint *samps, const GLenum target) {
	tex_active(idx);
	glBindTexture(target, texs[idx]);
	glUniform1i(samps[idx], idx);

	const GLint wrap = GL_CLAMP_TO_EDGE;
	if (target == GL_TEXTURE_3D) {
		glTexParameteri(target, GL_TEXTURE_WRAP_R, wrap);
	}
	glTexParameteri(target, GL_TEXTURE_WRAP_S, wrap);
	glTexParameteri(target, GL_TEXTURE_WRAP_T, wrap);
}

static void setup_texture_cms(const GLint idx, const GLuint *texs,
const GLint *samps) {
	enable_bind_tex(idx, texs, samps, GL_TEXTURE_3D);
	tex_filter(GL_TEXTURE_3D, 0, gl_min_nearest, gl_mag_best);
}

static void setup_texture2d(const GLint idx, const GLuint *texs,
const GLint *samps) {
	enable_bind_tex(idx, texs, samps, GL_TEXTURE_2D);
	tex_filter(GL_TEXTURE_2D, WU_MIPMAP_MAX, gl_min_linear, gl_mag_fast);
}

static bool check_uniforms(const GLint *uniforms, const size_t len) {
	for (size_t i = 0; i < len; ++i) {
		if (uniforms[i] == -1) {
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
	return object * (status == GL_TRUE);
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

#ifdef WU_GL_DEBUG
static void debug_print(GLenum source, GLenum type, GLuint id, GLenum severity,
GLsizei len, const GLchar *message, const void *user_data) {
	(void)source;
	(void)type;
	(void)id;
	(void)severity;
	(void)user_data;
	print_gl_message(message, len);
}
#endif /* WU_GL_DEBUG */

bool gl_context_setup(struct gl_context *context, struct wu_conf *wuconf) {
#ifdef WU_GL_DEBUG
	fprintf(stderr, "vendor: %s\n"
		"renderer: %s\n"
		"version: %s\n"
		"shading: %s\n",
		glGetString(GL_VENDOR), glGetString(GL_RENDERER),
		glGetString(GL_VERSION), glGetString(GL_SHADING_LANGUAGE_VERSION));
	glDebugMessageCallback(debug_print, NULL);
	glEnable(GL_DEBUG_OUTPUT);
#endif /* WU_GL_DEBUG */

	const char vs[] =
		"#version 330 core\n"
		"in vec2 " ATTR_POS ";"
		"out vec2 texcoord;"
		"uniform mat4 " UNI_POS_MATRIX ";"
		"void main() {"
			"texcoord = " ATTR_POS " * vec2(0.5) + vec2(0.5);"
			"gl_Position = " UNI_POS_MATRIX " * vec4(" ATTR_POS ", 0, 1);"
		"}";
	const char fs[] =
		"#version 330 core\n"
		"in vec2 texcoord;"
		"in float zoom;"
		"out vec4 color;"

		"uniform sampler2D " UNI_IMG ";"
		"uniform sampler2D " UNI_PAL ";"
		"uniform sampler2D " UNI_PLANE3 ";"
		"uniform sampler2D " UNI_PLANE4 ";"
		"uniform sampler3D " UNI_CMS_LUT ";"

		"uniform int " UNI_COLOR_MODE ";"
		"uniform int " UNI_CMS_MODE ";"
		"uniform int[2] " UNI_ALPHA_OP ";"
		"uniform vec4 " UNI_PLANE_OFFSETS ";"
		"uniform mat4 " UNI_TO_RGBA ";"
		"uniform mat3 " UNI_CMS_MAT ";"
		"uniform int " UNI_TRANSFER ";"
		"uniform float[5] " UNI_ARGS ";"

		"void gen_check_pattern() {"
			"vec2 d = floor(texcoord / fwidth(texcoord) * vec2(1./16.));"
			"float s = (d.x + d.y) * .5;"
			"float bg = (s - floor(s)) * .5 + .5;"
			"color.rgb += vec3(bg - bg * color.a);"
		"}"
		"float linear_gamma(float c, float[5] arg) {"
			"if (c > arg[0]) {"
				"return pow(c * arg[1] + arg[2], arg[3]);"
			"}"
			"return c * arg[4];"
		"}"
		"float log_transfer(float c, float[5] arg) {"
			"if (c > arg[0]) {"
				"return exp2(c * arg[1] - arg[1]);"
			"}"
			"return c;"
		"}"
		"vec3 perceptual_quantization(vec3 c, float[5] arg) {"
			"vec3 nc = pow(c, vec3(arg[1]));"
			"return pow("
				"max(nc - vec3(arg[2]), 0)"
					"/ (vec3(arg[3]) - vec3(arg[4])*nc),"
				"vec3(arg[0]));"
		"}"
		"float hybrid_log_gamma(float c, float[5] arg) {"
			"if (c > 0.5) {"
				"return exp2(c * arg[0] + arg[1]) + arg[2];"
			"}"
			"return c * c * (1.0 / 3.0);"
		"}"
		"vec3 eotf(vec3 c) {"
			"switch (" UNI_TRANSFER ") {"
			"case " TRANSFER_LINEAR_GAMMA ":"
				"for (int i = 0; i < 3; ++i) {"
					"c[i] = linear_gamma(c[i]," UNI_ARGS ");"
				"}"
				"break;"
			"case " TRANSFER_LOG ":"
				"for (int i = 0; i < 3; ++i) {"
					"c[i] = log_transfer(c[i]," UNI_ARGS ");"
				"}"
				"break;"
			"case " TRANSFER_PQ ":"
				"c = perceptual_quantization(c," UNI_ARGS ");"
				"break;"
			"case " TRANSFER_HLG ":"
				"for (int i = 0; i < 3; ++i) {"
					"c[i] = hybrid_log_gamma(c[i]," UNI_ARGS ");"
				"}"
				"break;"
			"}"
			"return c;"
		"}"
		"void main() {"
			"if (" UNI_COLOR_MODE "==" COLOR_PLANAR ") {"
				"color = (vec4("
					"texture(" UNI_IMG ", texcoord).r,"
					"texture(" UNI_PAL ", texcoord).r,"
					"texture(" UNI_PLANE3 ", texcoord).r,"
					"texture(" UNI_PLANE4 ", texcoord).r"
				") +" UNI_PLANE_OFFSETS ") *" UNI_TO_RGBA ";"
			"} else {"
				"color = texture(" UNI_IMG ", texcoord);"
				"if (" UNI_COLOR_MODE "==" COLOR_PALETTE ") {"
					"color = texture(" UNI_PAL ","
						"vec2(color.r, 0.0));"
				"}"
			"}"

			"if (" UNI_CMS_MODE "==" CMS_LUT ") {"
				"color.rgb = texture("
					UNI_CMS_LUT ", color.rgb).rgb;"
			"} else {"
				"color.rgb = eotf(color.rgb);"
				"if (" UNI_CMS_MODE "==" CMS_SPACEWALK ") {"
					"color.rgb *=" UNI_CMS_MAT ";"
				"}"
			"}"

//			"color.rgb = pow(color.rgb, vec3(0.454545));"
			"if (bool(" UNI_ALPHA_OP "[0])) {"
				"color.rgb *= color.aaa;"
			"}"
			"switch (" UNI_ALPHA_OP "[1]) {"
			"case 2:"
				"gen_check_pattern();" // fallthrough
			"case 1:"
				"color.a = 1.0;"
			"}"
		"}";
	const GLuint vshader = setup_shader(vs, GL_VERTEX_SHADER);
	if (vshader == 0) {
		return false;
	}
	const GLuint fshader = setup_shader(fs, GL_FRAGMENT_SHADER);
	if (fshader == 0) {
		return false;
	}
	const GLuint program = setup_program(vshader, fshader);
	if (program == 0) {
		return false;
	}
	glUseProgram(program);
	glDeleteProgram(program);

	GLint al = glGetAttribLocation(program, ATTR_POS);
	if (al < 0) {
		return false;
	}
	const GLint samps[] = {
		glGetUniformLocation(program, UNI_IMG),
		glGetUniformLocation(program, UNI_PAL),
		glGetUniformLocation(program, UNI_PLANE3),
		glGetUniformLocation(program, UNI_PLANE4),
		glGetUniformLocation(program, UNI_CMS_LUT),
	};
	const GLint uni[] = {
		glGetUniformLocation(program, UNI_POS_MATRIX),
		glGetUniformLocation(program, UNI_COLOR_MODE),
		glGetUniformLocation(program, UNI_ALPHA_OP),
		glGetUniformLocation(program, UNI_CMS_MODE),
		glGetUniformLocation(program, UNI_PLANE_OFFSETS),
		glGetUniformLocation(program, UNI_TO_RGBA),
		glGetUniformLocation(program, UNI_CMS_MAT),
		glGetUniformLocation(program, UNI_TRANSFER),
		glGetUniformLocation(program, UNI_ARGS),
	};
	if (!check_uniforms(samps, ARRAY_LEN(samps))
	|| !check_uniforms(uni, ARRAY_LEN(uni)) ) {
		return false;
	}

	context->uni = (struct gl_uni) {
		.pos_matrix = uni[0],
		.color_mode = uni[1],
		.alpha_op = uni[2],
		.cms_mode = uni[3],
		.plane_offsets = uni[4],
		.to_rgba = uni[5],
		.cms_mat = uni[6],
		.transfer = uni[7],
		.args = uni[8],
	};

	GLuint vertex_array;
	glGenVertexArrays(1, &vertex_array);
	glBindVertexArray(vertex_array);

	GLuint array_buf[2];
	glGenBuffers(ARRAY_LEN(array_buf), array_buf);
	context->pixel_unpack_buf = array_buf[0];

	const GLbyte vertices[] = {
		-1,-1,  1,-1,
		-1, 1,  1, 1,
	};
	bind_buffer_data(GL_ARRAY_BUFFER, array_buf[1], sizeof(vertices),
		vertices, GL_STATIC_DRAW);
	GLuint attrib = (GLuint)al;
	glVertexAttribPointer(attrib, 2, GL_BYTE, GL_FALSE, 0, 0);
	glEnableVertexAttribArray(attrib);


	GLuint texs[ARRAY_LEN(samps)];
	glGenTextures(ARRAY_LEN(texs), texs);

	GLint s = (GLint)ARRAY_LEN(samps) - 1;
	setup_texture_cms(s, texs, samps);
	while (s > 0) {
		--s;
		setup_texture2d(s, texs, samps);
	}

	glEnable(GL_BLEND);
	glEnable(GL_FRAMEBUFFER_SRGB);
	glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
	if (wuconf->bg_src == bg_default) {
		gl_clear_color(wuconf->bg);
	}

	GLuint mts;
	glGetIntegerv(GL_MAX_TEXTURE_SIZE, (GLint *)&mts);
#ifdef WU_GL_DEBUG
	printf("Texture size limit: %u\n", mts);
#endif
	if (wuconf->max_img_size) {
		wuconf->max_img_size = umin(wuconf->max_img_size, mts);
	} else {
		wuconf->max_img_size = mts;
	}

	glGenQueries(1, &context->timer);
	return true;
}

#include <stdio.h>
#include <math.h>
#include <limits.h>
#include <string.h>

#include <epoxy/gl.h>

#include "wudefs.h"
#include "common.h"
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

#define UNI_POS_MATRIX "pos_matrix"
#define UNI_COLOR_MODE "color_mode"
#define UNI_ALPHA_MODE "alpha_mode"
#define UNI_PLANE_OFFSETS "plane_offsets"
#define UNI_COLORSPACE "colorspace"

#define MODE_RAW "0"
#define MODE_PALETTE "1"
#define MODE_PLANAR "2"

#define ALPHA_ENABLED "0"
#define ALPHA_CHECKERS "1"
#define ALPHA_OPAQUE "2"

static const GLint WU_MIPMAP_MAX = 6;
static const GLint WU_MIN_FILTER = GL_LINEAR_MIPMAP_LINEAR;

enum gl_tex_unit {
	gl_tex_img = GL_TEXTURE0,
	gl_tex_pal = GL_TEXTURE1,
	gl_tex_plane3 = GL_TEXTURE2,
	gl_tex_plane4 = GL_TEXTURE3,
	gl_tex_reader = GL_TEXTURE4,
};

struct gl_upload_params {
	GLint in_fmt;
	GLenum fmt, type;
	enum pix_layout layout:8;
	enum unpack_op op:8;
	bool disable_alpha;
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

GLuint64 gl_clock_end(const struct gl_context *context) {
	GLuint64 ns = 0;
	glEndQuery(GL_TIME_ELAPSED);
	glGetQueryObjectui64v(context->timer, GL_QUERY_RESULT, &ns);
	return ns;
}

void gl_clock_start(const struct gl_context *context) {
	glBeginQuery(GL_TIME_ELAPSED, context->timer);
}

void gl_alpha_toggle(struct gl_context *context) {
	if (!context->disable_alpha) {
		context->alpha = (enum gl_alpha_mode)(
			(context->alpha + 1) % gl_alpha_STATES);
		glUniform1i(context->uni.alpha_mode, context->alpha);
	}
}

static float fix_aspect_ratio(GLfloat *mat, const struct gl_context *context,
const unsigned char rotation) {
	// Scale the image to its natural size, taking rotation into account.
	const int r1 = rotation & 1;
	const float ratio_w = (float)context->tex.w / context->fb_wh[r1];
	const float ratio_h = (float)context->tex.h / context->fb_wh[r1^1];

	if (mat) {
		const int r2 = 5 - r1;
		mat[r1] *= ratio_w;
		mat[r2] *= ratio_h;
	}
	return 1 / fmaxf(ratio_w, ratio_h);
}

static int bool_to_sign(const bool val) {
	return val * 2 - 1;
}

static GLfloat hard_math(const int rotate) {
	return (GLfloat)( (rotate & 1) * -bool_to_sign(rotate & 2) );
}

static void set_mirrot(GLfloat *mat, const int rotate, const bool mirror) {
	// Do some hard math to apply both rotation and mirroring
	const GLfloat cosy = hard_math(rotate + 1);
	const GLfloat sinner = hard_math(rotate);
	const GLfloat mirror_mult = (GLfloat)bool_to_sign(mirror);

	/* GL textures are bottom-up. We switch the signs of mirror_mult to
	 * flip to top-down. */
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

	set_mirrot(mat, state->rotate, state->mirror);
	state->fit_zoom = fix_aspect_ratio(mat, context, state->rotate);

	/* We receive input measured in pixels from the top left corner, which
	 * is sort of like the interval 0..1, but GL renders from the center
	 * between -1..1, so we multiply by 2 to keep things working. */
	const float scale = 2;
	/* It seems having exact integer offsets causes ugly artifacts
	 * when rendering. We add a fraction of a pixel to remedy this. */
	const float fix = 1.0f / 17.0f;
	mat[12] += (floorf( state->x_offset*scale) + fix) / context->fb_wh[0];
	mat[13] += (floorf(-state->y_offset*scale) + fix) / context->fb_wh[1];
	mat[15] = 1 / state->zoom;

	glUniformMatrix4fv(context->uni.pos_matrix, 1, GL_FALSE, mat);
	context->update_matrix = false;
}

float gl_fit_zoom(const struct gl_context *context,
const unsigned char rotation) {
	return fix_aspect_ratio(NULL, context, rotation);
}

void gl_viewport(struct gl_context *context, const struct display_dims *dims) {
	context->fb_wh[0] = (float)dims->w;
	context->fb_wh[1] = (float)dims->h;
	context->update_matrix = true;
	glViewport(0, 0, (int)dims->w, (int)dims->h);
}

static void swizzle_set(const enum pix_layout layout) {
	const GLint lut[pix_color_total] = {GL_RED, GL_GREEN, GL_BLUE, GL_ALPHA};
	GLint swizzle[pix_color_total];
	for (enum pix_color i = 0; i < pix_color_total; ++i) {
		swizzle[i] = lut[pix_layout_offset(layout, i)];
	}
	glTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_RGBA, swizzle);
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

static void tex_sub2d_params(const struct gl_upload_params *p, const size_t x,
const size_t y, size_t w, const size_t h, const void *data) {
	tex_sub2d((GLsizei)x, (GLsizei)y, (GLsizei)w, (GLsizei)h,
		p->fmt, p->type, data);
	swizzle_set(p->layout);
}

static void tex_2d_params(const struct gl_upload_params *p, const size_t w,
const size_t h, const void *data) {
	tex_2d(p->in_fmt, (GLsizei)w, (GLsizei)h, p->fmt, p->type, data);
	swizzle_set(p->layout);
}

static void tex_2d_null(void) {
	tex_2d(GL_RED, 0, 0, GL_RED, GL_UNSIGNED_BYTE, NULL);
}

static void tex_2d_solid(void) {
	const uint8_t c = 0xff;
	tex_2d(GL_RED, 1, 1, GL_RED, GL_UNSIGNED_BYTE, &c);
}

static void tex_parameteri(const GLenum name, const GLint param) {
	glTexParameteri(GL_TEXTURE_2D, name, param);
}

static void bind_buffer_data(const GLenum target, const GLuint buffer,
const size_t size, const GLvoid *data, const GLenum usage) {
	glBindBuffer(target, buffer);
	glBufferData(target, (GLsizeiptr)size, data, usage);
}

static void * map_unpack_buffer(const GLuint pix_buf, const size_t size) {
	bind_buffer_data(GL_PIXEL_UNPACK_BUFFER, pix_buf, size, NULL,
		GL_STREAM_DRAW);
	return glMapBuffer(GL_PIXEL_UNPACK_BUFFER, GL_WRITE_ONLY);
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
	glActiveTexture(gl_tex_pal);
	GLint filter, level;
	if (enable) {
		filter = GL_NEAREST;
		level = 0;
		tex_2d(GL_RGBA, 256, 1, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	} else {
		filter = WU_MIN_FILTER;
		level = WU_MIPMAP_MAX;
		tex_2d_null();
	}
	tex_parameteri(GL_TEXTURE_MAX_LEVEL, level);
	tex_parameteri(GL_TEXTURE_MIN_FILTER, filter);

	glActiveTexture(gl_tex_img);
	tex_parameteri(GL_TEXTURE_MIN_FILTER, filter);
}

static void switch_color_mode(struct gl_context *context,
const enum image_mode new_mode) {
	if (new_mode != context->tex.mode) {
		switch (context->tex.mode) {
		case image_mode_raw: break;
		case image_mode_palette:
			palette_parameters(false);
			break;
		case image_mode_planar:
			if (new_mode != image_mode_palette) {
				glActiveTexture(gl_tex_pal);
				tex_2d_null();
			}
			glActiveTexture(gl_tex_plane3);
			tex_2d_null();
			glActiveTexture(gl_tex_plane4);
			tex_2d_null();
			glActiveTexture(gl_tex_img);
			break;
		}

		switch (new_mode) {
		case image_mode_raw: break;
		case image_mode_palette:
			palette_parameters(true);
			break;
		case image_mode_planar:
			break;
		}

		context->tex.mode = new_mode;
		glUniform1i(context->uni.color_mode, new_mode);
	}
}

static void plane_colorspace_conversion(const struct gl_uni *uni,
const enum color_space space, enum pix_layout layout) {
	struct color_mat cm;
	color_mat_gen(&cm, space, layout);

	glUniform4f(uni->plane_offsets,
		cm.off[0], cm.off[1], cm.off[2], cm.off[3]);
	glUniformMatrix4fv(uni->colorspace, 1, GL_FALSE, cm.mat);
}

static void * unpack_upload(const GLuint pix_buf, const enum unpack_op op,
const struct raw_img *img, const size_t w, const size_t h,
const unsigned char *data) {
	size_t instride = scanline_length(w, img->bitdepth, img->alignment);
	size_t outstride = unpack_stride(w, img->bitdepth, img->attr, op);
	if (!outstride) {
		if (op == op_noop) {
			outstride = instride;
		} else {
			fatal_bug("Upload failure", "Unsupported raster format");
		}
	}

	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	unsigned char *map = map_unpack_buffer(pix_buf, outstride * h);
	for (size_t y = 0; y < h; ++y) {
		unpack_or_copy_strip(map + outstride*y, data + instride*y,
			w, img->bitdepth, img->attr, op);
	}
	glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER);
	return 0;
}

static enum gl_upload_status tex_upload(struct gl_context *context,
const struct raw_img *img, const struct gl_upload_params *params,
const size_t w, const size_t h, const void *data, const bool reuse) {
	const GLuint pix_buf = context->pixel_unpack_buf;
	bool bind_buffer = false;
	const size_t elems = w * params->comps;
	if (params->op != op_noop || img->alignment > 8) {
		data = unpack_upload(pix_buf, params->op, img, elems, h, data);
		bind_buffer = true;
	} else {
		glPixelStorei(GL_UNPACK_ALIGNMENT, img->alignment);
	}

	if (reuse) {
		tex_sub2d_params(params, 0, 0, w, h, data);
	} else {
		tex_2d_params(params, w, h, data);
	}

	if (bind_buffer) {
		glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
	}
	return reuse ? gl_upload_reused : gl_upload_success;
}

static enum gl_upload_status planar_upload(struct gl_context *context,
const struct raw_img *img, const struct gl_upload_params *params,
const bool reuse) {
	const struct plane_info *plane = img->u.planes->p;
	enum gl_upload_status status = gl_upload_fail;
	for (GLenum i = 0; i < 4; ++i) {
		glActiveTexture(GL_TEXTURE0 + i);
		if (i < img->channels) {
			status = tex_upload(context, img, params, plane[i].w,
				plane[i].h, plane[i].ptr, reuse);
			if (status == gl_upload_fail) {
				break;
			}
		} else {
			tex_2d_solid();
		}
	}
	glActiveTexture(gl_tex_img);

	plane_colorspace_conversion(&context->uni, img->u.planes->cs, img->layout);
	return status;
}

static int reusable_texture(const struct gl_context *context,
const struct raw_img *img) {
	const int hash = raw_img_geom_hash(img);
	const bool reuse = img->w == context->tex.w
		&& img->h == context->tex.h
		&& hash == context->tex.hash;
	return (reuse) ? 0 : hash;
}

static enum gl_upload_status mode_upload(struct gl_context *context,
const struct raw_img *img, const struct gl_upload_params *params) {
	const int hash = reusable_texture(context, img);

	enum gl_upload_status status = gl_upload_fail;
	switch (img->mode) {
	case image_mode_palette:
		glActiveTexture(gl_tex_pal);
		swizzle_set(img->layout);
		tex_sub2d(0, 0, 256, 1, GL_RGBA, GL_UNSIGNED_BYTE, img->u.palette);
		glActiveTexture(gl_tex_img);
		// fallthrough
	case image_mode_raw:
		status = tex_upload(context, img, params, img->w, img->h,
			img->data, !hash);
		break;
	case image_mode_planar:
		status = planar_upload(context, img, params, !hash);
		break;
	}

	if (status != gl_upload_fail && hash) {
		context->tex.w = img->w;
		context->tex.h = img->h;
		context->tex.hash = hash;
	}
	return status;
}

static bool set_upload_params(struct gl_upload_params *params,
const struct raw_img *img) {
	const uint8_t ch = params->comps;
	const uint8_t bd = img->bitdepth;
	bool is_float = false;
	switch (img->attr) {
	case pix_packing_332:
		params->in_fmt = GL_R3_G3_B2;
		params->fmt = GL_RGB;
		params->type = GL_UNSIGNED_BYTE_3_3_2;
		return true;
	case pix_packing_1555:
		params->in_fmt = GL_RGB5_A1;
		params->fmt = GL_BGRA;
		params->type = GL_UNSIGNED_SHORT_1_5_5_5_REV;
		params->layout = layout_equiv(params->layout, true, false);
		return true;
	case pix_float:
		is_float = true;
		switch (bd) {
		case 16: case 32:
			break;
		case 64:
			params->op = op_pack;
			break;
		default:
			return false;
		}
		break;
	case pix_inverted:
		switch (bd) {
		case 1: case 2: case 4: case 8:
			params->op = (img->mode == image_mode_palette)
				? op_unpack : op_expand;
			break;
		default:
			return false;
		}
		break;
	case pix_normal:
		switch (bd) {
		case 4:
			if (ch == 4) {
				params->in_fmt = GL_RGBA4;
				params->fmt = GL_BGRA;
				params->type = GL_UNSIGNED_SHORT_4_4_4_4_REV;
				params->layout = layout_equiv(params->layout,
					true, true);
				return true;
			}
			// fallthrough
		case 1: case 2:
			params->op = (img->mode == image_mode_palette)
				? op_unpack : op_expand;
			break;
		case 24: case 64:
			params->op = op_pack;
			break;
		case 8: case 16: case 32:
			break;
		default:
			return false;
		}
		break;
	default:
		return false;
	}

	const GLint in_fmt_lut[] = {GL_RED, GL_RG, GL_RGB, GL_RGBA};
	const GLenum fmt_lut[] = {GL_RED, GL_RG, GL_BGR, GL_BGRA};
	const GLenum type_lut[] = {
		GL_UNSIGNED_BYTE,
		(is_float) ? GL_HALF_FLOAT : GL_UNSIGNED_SHORT,
		GL_UNSIGNED_SHORT, // packed 24bpc
		(is_float) ? GL_FLOAT : GL_UNSIGNED_INT,
	};

	const int depth = iclamp(bd / 8, 1, 4);
	params->in_fmt = in_fmt_lut[ch - 1];
	params->fmt = fmt_lut[ch - 1];
	params->type = type_lut[depth - 1];
	if (ch >= 3) {
		params->layout = layout_equiv(params->layout, true, false);
	}
	return true;
}

enum gl_upload_status gl_texture_upload(struct gl_context *context,
const struct raw_img *img) {
	struct gl_upload_params params = {.op = op_noop};
	switch_color_mode(context, img->mode);
	if (img->mode == image_mode_raw) {
		params.layout = img->layout;
		params.comps = img->channels;
	} else {
		params.layout = pix_gray;
		params.comps = 1;
	}

	if (!set_upload_params(&params, img)) {
		printf("Invalid channel/bitdepth combination (%d/%d)\n",
			img->channels, img->bitdepth);
		return gl_upload_fail;
	}

	const enum gl_upload_status status = mode_upload(context, img, &params);
	const GLenum err = glGetError();
	if (err) {
		printf("Encountered error %x when uploading to texture: %s\n",
			err, gl_strerror(err));
		return gl_upload_fail;
	}

	context->disable_alpha = img->disable_alpha;
	glUniform1i(context->uni.alpha_mode,
		(context->disable_alpha) ? gl_alpha_opaque : context->alpha);
	return status;
}

void gl_draw(void) {
	glClear(GL_COLOR_BUFFER_BIT);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

void gl_clear_color(const float bg[static 4]) {
	glClearColor(bg[0], bg[1], bg[2], bg[3]);
}

void gl_reader_read_row(struct gl_context *context, struct wu_state *state,
const struct gl_reader *reader, void *restrict data, const size_t row) {
	state->y_offset = (float)row;
	gl_matrix_update(context, state);
	gl_draw();
	glReadPixels(0, 0, (GLsizei)reader->w, 1, reader->fmt, reader->type,
		data);
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
	r->ch = (img->mode == image_mode_palette) ? 4 : img->channels;
	r->bd = (img->bitdepth > 8) ? 16 : 8;
	r->fmt = fmts[r->ch - 1];
	r->type = (img->bitdepth > 8) ? GL_UNSIGNED_SHORT : GL_UNSIGNED_BYTE;
	r->len = r->w * r->ch * (r->bd / 8);

	const GLint in_fmt = in_fmts[img->bitdepth > 8][r->ch - 1];

	glActiveTexture(gl_tex_reader);
	tex_2d(in_fmt, (GLsizei)r->w, 1, r->fmt, r->type, NULL);
	glActiveTexture(gl_tex_img);

	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
		return false;
	}

	state->rotate = img->rotate;
	state->mirror = !img->mirror; /* At this point I don't know why this
		extra flip is needed. */

	struct display_dims dims = {
		.w = (unsigned)r->w,
		.h = (unsigned)r->h,
	};
	gl_viewport(context, &dims);

	if (img->layout == pix_gray) {
		swizzle_set(pix_rgba);
	}
	return true;
}

void gl_reader_disable(struct gl_context *context,
const struct gl_reader *reader) {
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	gl_viewport(context, &reader->prev);
}

void gl_reader_enable(struct gl_context *context, struct gl_reader *reader) {
	GLint dims[4];
	glGetIntegerv(GL_VIEWPORT, dims);
	reader->prev = (struct display_dims) {
		.w = (unsigned)dims[2],
		.h = (unsigned)dims[3],
	};

	if (context->framebuffer) {
		glBindFramebuffer(GL_FRAMEBUFFER, context->framebuffer);
	} else {
		glGenFramebuffers(1, &context->framebuffer);
		glBindFramebuffer(GL_FRAMEBUFFER, context->framebuffer);

		GLuint tex;
		glGenTextures(1, &tex);
		glActiveTexture((GLenum)gl_tex_reader);
		glBindTexture(GL_TEXTURE_2D, tex);
		tex_parameteri(GL_TEXTURE_MAX_LEVEL, 0);

		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
			GL_TEXTURE_2D, tex, 0);
		glActiveTexture(gl_tex_img);
	}
}

static void setup_texture(const GLint idx, const GLuint *texs,
const GLint *samps, const enum pix_layout layout) {
	const GLenum target = GL_TEXTURE_2D;
	glActiveTexture((GLenum)(GL_TEXTURE0 + idx));
	glBindTexture(target, texs[idx]);
	glUniform1i(samps[idx], idx);

	const GLint wrap = GL_CLAMP_TO_EDGE;
	tex_parameteri(GL_TEXTURE_WRAP_S, wrap);
	tex_parameteri(GL_TEXTURE_WRAP_T, wrap);
	tex_parameteri(GL_TEXTURE_MIN_FILTER, WU_MIN_FILTER);
	tex_parameteri(GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	tex_parameteri(GL_TEXTURE_MAX_LEVEL, WU_MIPMAP_MAX);
	swizzle_set(layout);
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
	GLchar logbuf[256];
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
		"out vec4 color;"

		"uniform sampler2D " UNI_IMG ";"
		"uniform sampler2D " UNI_PAL ";"
		"uniform sampler2D " UNI_PLANE3 ";"
		"uniform sampler2D " UNI_PLANE4 ";"

		"uniform int " UNI_COLOR_MODE ";"
		"uniform int " UNI_ALPHA_MODE ";"
		"uniform vec4 " UNI_PLANE_OFFSETS ";"
		"uniform mat4 " UNI_COLORSPACE ";"

		"vec4 planar_color() {"
			"return (vec4("
				"texture2D(" UNI_IMG ", texcoord).r,"
				"texture2D(" UNI_PAL ", texcoord).r,"
				"texture2D(" UNI_PLANE3 ", texcoord).r,"
				"texture2D(" UNI_PLANE4 ", texcoord).r"
			") +" UNI_PLANE_OFFSETS ") *" UNI_COLORSPACE ";"
		"}"
		"vec4 check_pattern(vec4 fg) {"
			"vec2 d = floor(texcoord / (fwidth(texcoord) * 16.0));"
			"vec3 bg = vec3(mod(d.x + d.y, 2.0) * .25 + .5);"
			"return vec4(mix(bg, fg.rgb, fg.a), 1.0);"
		"}"
		"void main() {"
			"if (" UNI_COLOR_MODE "==" MODE_PLANAR ") {"
				"color = planar_color();"
			"} else {"
				"color = texture2D(" UNI_IMG ", texcoord);"
				"if (" UNI_COLOR_MODE "==" MODE_PALETTE ") {"
					"color = texture2D(" UNI_PAL ","
						"vec2(color.r, 0));"
				"}"
			"}"
			"switch (" UNI_ALPHA_MODE ") {"
			"case " ALPHA_CHECKERS ":"
				"color = check_pattern(color);"
				"break;"
			"case " ALPHA_OPAQUE ":"
				"color.a = 1.0;"
				"break;"
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
	};
	const GLint uni[] = {
		glGetUniformLocation(program, UNI_POS_MATRIX),
		glGetUniformLocation(program, UNI_COLOR_MODE),
		glGetUniformLocation(program, UNI_ALPHA_MODE),
		glGetUniformLocation(program, UNI_PLANE_OFFSETS),
		glGetUniformLocation(program, UNI_COLORSPACE),
	};
	if (!check_uniforms(samps, ARRAY_LEN(samps))
	|| !check_uniforms(uni, ARRAY_LEN(uni)) ) {
		return false;
	}

	context->uni = (struct gl_uni) {
		.pos_matrix = uni[0],
		.color_mode = uni[1],
		.alpha_mode = uni[2],
		.plane_offsets = uni[3],
		.colorspace = uni[4],
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

	for (GLint i = (GLint)ARRAY_LEN(samps); i > 0; --i) {
		setup_texture(i - 1, texs, samps, pix_gray);
	}


	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	if (!wuconf->bg_src) {
		GLfloat bg[4];
		for (size_t i = 0; i < sizeof(wuconf->bg); ++i) {
			bg[i] = (float)wuconf->bg[i] / (float)UCHAR_MAX;
		}
		gl_clear_color(bg);
	}

	GLuint mts;
	glGetIntegerv(GL_MAX_TEXTURE_SIZE, (GLint *)&mts);
	if (wuconf->max_img_size) {
		wuconf->max_img_size = umin(wuconf->max_img_size, mts);
	} else {
		wuconf->max_img_size = mts;
	}

	glGenQueries(1, &context->timer);
	return true;
}

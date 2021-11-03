#include <stdio.h>
#include <math.h>
#include <limits.h>
#include <string.h>

#include <epoxy/gl.h>

#include "wudefs.h"
#include "common.h"
#include "opengl.h"
#include "raster/pix.h"
#include "raster/unpack.h"

#define WU_GL_DEBUG

#define ATTR_POS "pos"
#define UNI_IMG "img"
#define UNI_PAL "pal"
#define UNI_PLANE3 "plane3"
#define UNI_PLANE4 "plane4"
#define UNI_MATRIX "matrix"
#define UNI_COLOR_MODE "color_mode"
#define UNI_ENABLE_CHECKERS "enable_checkers"

#define MODE_RAW "0"
#define MODE_PALETTE "1"
#define MODE_YUVA "2"

enum gl_tex_unit {
	gl_tex_img = GL_TEXTURE0,
	gl_tex_pal = GL_TEXTURE0 + 1,
	gl_tex_plane3 = GL_TEXTURE0 + 2,
	gl_tex_plane4 = GL_TEXTURE0 + 3,
};

struct gl_upload_params {
	GLenum type, fmt;
	GLint in_fmt;
	enum pix_layout layout:8;
	bool convert;
};

static const char * gl_strerror(const GLenum error) {
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

void gl_context_delete(struct gl_context *context) {
	GLint texs[4];
	for (size_t i = 0; i < ARRAY_LEN(texs); ++i) {
		glActiveTexture(GL_TEXTURE0 + (GLenum)i);
		glGetIntegerv(GL_TEXTURE_BINDING_2D, texs + i);
	}
	glDeleteTextures(ARRAY_LEN(texs), (GLuint *)texs);

	GLint bufs[2];
	bufs[0] = (GLint)context->pixel_unpack_buf;
	glGetIntegerv(GL_ARRAY_BUFFER_BINDING, bufs + 1);
	glDeleteBuffers(ARRAY_LEN(bufs), (GLuint *)bufs);

	GLint objs[1];
	glGetIntegerv(GL_VERTEX_ARRAY_BINDING, objs);
	glDeleteVertexArrays(1, (GLuint *)objs);

	glDeleteQueries(1, &context->timer);
}

GLuint64 gl_clock_end(struct gl_context *context) {
	GLuint64 ns = 0;
	glEndQuery(GL_TIME_ELAPSED);
	glGetQueryObjectui64v(context->timer, GL_QUERY_RESULT, &ns);
	return ns;
}

void gl_clock_start(struct gl_context *context) {
	glBeginQuery(GL_TIME_ELAPSED, context->timer);
}

void gl_alpha_toggle(struct gl_context *context) {
	const enum gl_alpha alpha = (context->alpha + 1) % gl_alpha_STATES;

	if (context->tex.alpha_swizzle != GL_ONE) {
		if (context->tex.mode) {
			glActiveTexture(gl_tex_pal);
		}
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_A,
			(alpha == gl_alpha_opaque) ? GL_ONE : context->tex.alpha_swizzle);
		if (context->tex.mode) {
			glActiveTexture(gl_tex_img);
		}
	}

	glUniform1i(context->uni.checkers, alpha == gl_alpha_checkers);

	if (alpha == gl_alpha_disable) {
		glDisable(GL_BLEND);
	} else {
		glEnable(GL_BLEND);
	}

	context->alpha = alpha;
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
	// Do some complex math to apply both rotation and mirroring
	const GLfloat cosy = hard_math(rotate + 1);
	const GLfloat sinner = hard_math(rotate);
	const GLfloat mirror_mult = (GLfloat)bool_to_sign(mirror);

	/* GL textures are bottom-up. We switch the signs of mirror_mult to
	 * convert to top-down. */
	mat[0] = cosy;
	mat[1] = sinner;
	mat[4] = sinner * -mirror_mult;
	mat[5] = cosy * mirror_mult;
}

void gl_matrix_update(const struct gl_context *context,
struct wu_state *state) {
	GLfloat mat[16] = {
		1, 0, 0, 0,
		0, 1, 0, 0,
		0, 0, 1, 0,
		0, 0, 0, 1,
	};

	set_mirrot(mat, state->rotate, state->mirror);
	state->fit_zoom = fix_aspect_ratio(mat, context, state->rotate);

	/* We receive input measured in pixels from the top left corner, which
	 * is sort of like the interval [0, 1], but GL renders from the center
	 * between [-1, 1], so we multiply by 2 to keep things working. */
	const float scale = 2;
	/* It seems having integer offsets causes ugly artifacts
	 * when rendering. We add a fraction of a pixel to remedy this. */
	const float fix = 1.0f / 17.0f;
	mat[12] += (floorf( state->x_offset*scale) + fix) / context->fb_wh[0];
	mat[13] += (floorf(-state->y_offset*scale) + fix) / context->fb_wh[1];
	mat[15] = 1 / state->zoom;

	glUniformMatrix4fv(context->uni.matrix, 1, GL_FALSE, mat);
}

float gl_fit_zoom(const struct gl_context *context,
const unsigned char rotation) {
	return fix_aspect_ratio(NULL, context, rotation);
}

void gl_viewport(struct gl_context *context, const int w, const int h) {
	context->fb_wh[0] = (float)w;
	context->fb_wh[1] = (float)h;
	context->update_matrix = true;
	glViewport(0, 0, w, h);
}

static void tex_min_nearest(const bool nearest) {
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
		(nearest) ? GL_NEAREST : GL_LINEAR_MIPMAP_LINEAR);
}

static void tex_null(void) {
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, 0, 0, 0, GL_RED,
		GL_UNSIGNED_BYTE, NULL);
}

static void tex_2d(const struct gl_upload_params *params,
const size_t width, const size_t height, const unsigned char *restrict data) {
	glTexImage2D(GL_TEXTURE_2D, 0, params->in_fmt, (GLsizei)width,
		(GLsizei)height, 0, params->fmt, params->type, data);
	glGenerateMipmap(GL_TEXTURE_2D);
}

static void bind_buffer_data(const GLenum target, const GLuint buffer,
const size_t size, const GLvoid *data, const GLenum usage) {
	glBindBuffer(target, buffer);
	glBufferData(target, (GLsizeiptr)size, data, usage);
}

static GLint swizzle_set(const enum pix_layout layout, const bool alpha) {
	const GLint lut[pix_color_total] = {GL_RED, GL_GREEN, GL_BLUE, GL_ALPHA};
	GLint swizzle[pix_color_total];
	for (enum pix_color i = 0; i < pix_color_total; ++i) {
		if (i == 3 && !alpha) {
			swizzle[i] = GL_ONE;
		} else {
			swizzle[i] = lut[pix_layout_offset(layout, i)];
		}
	}
	glTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_RGBA, swizzle);
	return swizzle[3];
}
__attribute__((unused))
static void print_quaternary(const enum pix_layout layout) {
	char digits[4];
	for (size_t i = 0; i < ARRAY_LEN(digits); ++i) {
		const size_t sh = i*2;
		digits[i] = ((layout >> sh) & 0x3) + '0';
	}
	fwrite(digits, 1, sizeof(digits), stdout);
	putchar('\n');
}

static enum pix_layout layout_equiv(enum pix_layout layout,
const bool bgra_swap, const bool reverse) {
	/* On some cards GL_BGRA seems to be required for good texture upload
	 * performance, moreso for packed formats. We modify the layout to
	 * upload with good parameters and then fix the colors by swizzling. */
	enum pix_layout out = 0;
	for (enum pix_color i = 0; i < pix_color_total; ++i) {
		unsigned sh = i*2;
		uint8_t off = (layout >> sh) & 0x3;
		if (reverse) {
			sh = 6 - sh;
		}
		if (bgra_swap) {
			switch (sh) {
			case 0: sh = 4; break;
			case 4: sh = 0; break;
			}
		}
		out |= off << sh;
	}
	return out;
}

static bool upload_successful(void) {
	const GLenum err = glGetError();
	if (err) {
		printf("Encountered error %x when uploading to texture: %s\n",
			err, gl_strerror(err));
		return false;
	}
	return true;
}

static void palette_parameters(const bool enable) {
	tex_min_nearest(enable);
	glActiveTexture(gl_tex_pal);
	tex_min_nearest(enable);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, (enable) ? 0 : 6);
	glActiveTexture(gl_tex_img);
}

static enum gl_color_mode switch_color_mode(struct gl_context *context,
const struct raw_img *img) {
	enum gl_color_mode mode = gl_color_raw;
	if (img->yuva) {
		mode = gl_color_yuva;
	} else if (img->palette) {
		mode = gl_color_palette;
	}

	if (mode != context->tex.mode) {
		switch (context->tex.mode) {
		case gl_color_raw: break;
		case gl_color_palette:
			palette_parameters(false);
			break;
		case gl_color_yuva:
			glActiveTexture(gl_tex_plane3);
			tex_null();
			glActiveTexture(gl_tex_plane4);
			tex_null();
			if (mode != gl_color_palette) {
				glActiveTexture(gl_tex_pal);
				tex_null();
			}
			glActiveTexture(gl_tex_img);
			break;
		}

		switch (mode) {
		case gl_color_raw: break;
		case gl_color_palette:
			palette_parameters(true);
			break;
		case gl_color_yuva:
			break;
		}
		context->tex.mode = mode;
		glUniform1i(context->uni.color_mode, mode);
	}
	return mode;
}

static bool yuva_upload(struct gl_context *context, const struct raw_img *img) {
	struct yuva_info info;
	raw_img_yuva_info(img, &info);

	glPixelStorei(GL_UNPACK_ALIGNMENT, img->alignment);
	const struct gl_upload_params params = {
		.in_fmt = GL_RED,
		.fmt = GL_RED,
		.type = GL_UNSIGNED_BYTE,
	};
	swizzle_set(pix_gray, false);
	tex_2d(&params, info.ya.w, info.ya.h, info.yuva[0]);

	glActiveTexture(gl_tex_plane3);
	tex_2d(&params, info.uv.w, info.uv.h, info.yuva[1]);

	glActiveTexture(gl_tex_plane4);
	tex_2d(&params, info.uv.w, info.uv.h, info.yuva[2]);

	glActiveTexture(gl_tex_pal);
	if (info.yuva[3]) {
		tex_2d(&params, info.ya.w, info.ya.h, info.yuva[3]);
	} else {
		const unsigned char full = 0xff;
		tex_2d(&params, 1, 1, &full);
	}
	context->tex.alpha_swizzle = swizzle_set(pix_gray,
		(bool)info.yuva[3] && context->alpha != gl_alpha_opaque);

	glActiveTexture(gl_tex_img);
	if (!upload_successful()) {
		return gl_upload_fail;
	}

	context->tex.w = (unsigned)img->w;
	context->tex.h = (unsigned)img->h;
	context->tex.ch = 1;
	context->tex.bpp = 8;
	return gl_upload_success;
}

static bool set_upload_parameters(struct gl_context *context,
const struct raw_img *img, struct gl_upload_params *params) {
	enum pix_layout layout = img->layout;
	if (img->palette) {
		glActiveTexture(gl_tex_pal);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 256, 1, 0, GL_RGBA,
			GL_UNSIGNED_BYTE, img->palette);
		context->tex.alpha_swizzle = swizzle_set(layout,
			!img->disable_alpha);
		glActiveTexture(gl_tex_img);

		*params = (struct gl_upload_params) {
			.type = GL_UNSIGNED_BYTE,
			.fmt = GL_RED,
			.in_fmt = GL_RED,
			.layout = pix_gray,
			.convert = img->bitdepth < 8,
		};
		return true;
	}

	bool convert = false;
	switch (img->attr) {
	case pix_packing_332:
		*params = (struct gl_upload_params) {
			.type = GL_UNSIGNED_BYTE_3_3_2,
			.fmt = GL_RGB,
			.in_fmt = GL_R3_G3_B2,
			.convert = false,
			.layout = layout,
		};
		return true;
	case pix_packing_1555:
		*params = (struct gl_upload_params) {
			.type = GL_UNSIGNED_SHORT_1_5_5_5_REV,
			.fmt = GL_BGRA,
			.in_fmt = GL_RGB5_A1,
			.convert = false,
			.layout = layout_equiv(layout, true, false),
		};
		return true;
	case pix_float:
		switch (img->bitdepth) {
		case 16: case 32:
			break;
		case 64:
			convert = true;
			break;
		default:
			return false;
		}
		break;
	case pix_inverted:
		switch (img->bitdepth) {
		case 1: case 2: case 4: case 8:
			convert = true;
			break;
		default:
			return false;
		}
		break;
	case pix_normal:
		switch (img->bitdepth) {
		case 4:
			if (img->channels == 4) {
				*params = (struct gl_upload_params) {
					.type = GL_UNSIGNED_SHORT_4_4_4_4_REV,
					.fmt = GL_BGRA,
					.in_fmt = GL_RGBA4,
					.convert = false,
					.layout = layout_equiv(layout, true, true),
				};
				return true;
			}
			convert = true;
			break;
		case 1: case 2: case 24: case 64:
			convert = true;
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

	const bool is_float = img->attr == pix_float;
	const GLenum type_lut[] = {
		GL_UNSIGNED_BYTE,
		is_float ? GL_HALF_FLOAT : GL_UNSIGNED_SHORT,
		GL_UNSIGNED_INT, // expanded 24bpc
		is_float ? GL_FLOAT : GL_UNSIGNED_INT
	};
	const GLenum fmt_lut[] = {GL_RED, GL_RG, GL_BGR, GL_BGRA};
	const GLint in_fmt_lut[] = {GL_RED, GL_RG, GL_RGB, GL_RGBA};
	const int bd = imin((img->bitdepth + 7) / 8 - 1, 3);

	if (img->channels >= 3) {
		layout = layout_equiv(layout, true, false);
	}
	*params = (struct gl_upload_params) {
		.type = type_lut[bd],
		.fmt = fmt_lut[img->channels - 1],
		.in_fmt = in_fmt_lut[img->channels - 1],
		.convert = convert,
		.layout = layout,
	};
	return true;
}

static enum gl_upload_status tex_upload(struct gl_context *context,
const struct raw_img *img, const struct gl_upload_params *params,
const bool reuse) {
	const void *data;
	if (params->convert) {
		enum unpack_op op = op_expand;
		if (img->palette) {
			op = op_unpack;
		} else if (img->bitdepth > 32) {
			op = op_pack;
		}
		const size_t size = unpack_stride(img->w * img->channels,
			img->attr, op, img->bitdepth) * img->h;
		if (!size) {
			puts("Unsupported raster format! (This is a bug)");
			return gl_upload_fail;
		}

		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
		bind_buffer_data(GL_PIXEL_UNPACK_BUFFER,
			context->pixel_unpack_buf, size, NULL, GL_STATIC_DRAW);

		void *map = glMapBuffer(GL_PIXEL_UNPACK_BUFFER, GL_WRITE_ONLY);
		unpack_strip(map, img->data, img->w * img->channels, img->h,
			img->alignment, img->attr, op, img->bitdepth);
		glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER);
		data = 0;
	} else {
		glPixelStorei(GL_UNPACK_ALIGNMENT, img->alignment);
		data = img->data;
	}

	const GLsizei w = (GLsizei)img->w;
	const GLsizei h = (GLsizei)img->h;
	if (reuse) {
		glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h,
			params->fmt, params->type, data);
	} else {
		glTexImage2D(GL_TEXTURE_2D, 0, params->in_fmt, w, h, 0,
			params->fmt, params->type, data);
	}

	if (params->convert) {
		glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
	}

	if (!upload_successful()) {
		return gl_upload_fail;
	}

	const GLint alpha = swizzle_set(params->layout,
		!img->disable_alpha && context->alpha != gl_alpha_opaque);

	context->tex.w = (unsigned)img->w;
	context->tex.h = (unsigned)img->h;
	context->tex.ch = img->channels;
	context->tex.bpp = img->bitdepth;
	if (context->tex.mode == gl_color_raw) {
		context->tex.alpha_swizzle = alpha;
	}
	glGenerateMipmap(GL_TEXTURE_2D);
	return reuse ? gl_upload_reused : gl_upload_success;
}

enum gl_upload_status gl_texture_upload(struct gl_context *context,
const struct raw_img *img) {
	const enum gl_color_mode mode = switch_color_mode(context, img);
	if (mode == gl_color_yuva) {
		return yuva_upload(context, img);
	}

	struct gl_upload_params params;
	if (!set_upload_parameters(context, img, &params)) {
		printf("Invalid channel/bitdepth combination (%d/%d). Skipping.\n",
			img->channels, img->bitdepth);
		return gl_upload_fail;
	}
	const bool reusable = img->w == context->tex.w
		&& img->h == context->tex.h
		&& img->channels == context->tex.ch
		&& img->bitdepth == context->tex.bpp;
	return tex_upload(context, img, &params, reusable);
}

void gl_draw(void) {
	glClear(GL_COLOR_BUFFER_BIT);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

void gl_clear_color(const float bg[static 4]) {
	glClearColor(bg[0], bg[1], bg[2], bg[3]);
}

static void setup_texture(const GLint idx, const GLuint *texs,
const GLint *samps) {
	const GLenum target = GL_TEXTURE_2D;
	glActiveTexture((GLenum)(GL_TEXTURE0 + idx));
	glBindTexture(target, texs[idx]);
	glUniform1i(samps[idx], idx);

	const GLint wrap = GL_CLAMP_TO_EDGE;
	glTexParameteri(target, GL_TEXTURE_WRAP_S, wrap);
	glTexParameteri(target, GL_TEXTURE_WRAP_T, wrap);
	glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(target, GL_TEXTURE_MAX_LEVEL, 6);
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
		"uniform mat4 " UNI_MATRIX ";"
		"void main() {"
			"texcoord = " ATTR_POS " * vec2(0.5) + vec2(0.5);"
			"gl_Position = " UNI_MATRIX " * vec4(" ATTR_POS ", 0, 1);"
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
		"uniform bool " UNI_ENABLE_CHECKERS ";"
		"vec4 yuva_to_rgba() {"
			"vec3 yuv = vec3("
				"texture2D(" UNI_IMG ", texcoord).r,"
				"texture2D(" UNI_PLANE3 ", texcoord).r,"
				"texture2D(" UNI_PLANE4 ", texcoord).r"
			") - vec3(0.0625, 0.5, 0.5);"
/*			"vec3 k;"
			"k.b = 0.114;"
			"k.r = 0.299;"
			"k.g = 1 - k.b - k.r;"
			"float lum = 255/(235-16);"
			"float chr = 255/(240-16);"
			"float two_b = (2 - 2*k.b) * chr;"
			"float two_r = (2 - 2*k.r) * chr;"
			"mat3 color_mat = mat3("
				"  lum,               lum,    lum,"
				"    0,  -k.b/k.g * two_b,  two_b,"
				"two_r,  -k.r/k.g * two_r,      0"
			");"*/
			"mat3 bt601 = mat3("
				"1.1643,  1.16430, 1.1643,"
				"0.0,    -0.39173, 2.0170,"
				"1.5958, -0.81290, 0.0"
			");"
			"return vec4(vec3(bt601 * yuv),"
				"texture2D(" UNI_PAL ", texcoord).r);"
		"}"
		"vec4 check_pattern(vec4 fg) {"
			"vec2 d = floor(texcoord / (fwidth(texcoord) * 16));"
			"vec3 bg = vec3(mod(d.x + d.y, 2.0) * .25 + .5);"
			"return vec4(mix(bg, fg.rgb, fg.a), 1);"
		"}"
		"void main() {"
			"if (" UNI_COLOR_MODE "==" MODE_YUVA ") {"
				"color = yuva_to_rgba();"
			"} else {"
				"color = texture2D(" UNI_IMG ", texcoord);"
				"if (" UNI_COLOR_MODE "==" MODE_PALETTE ") {"
					"color = texture2D(" UNI_PAL ","
						"vec2(color.r, 0));"
				"}"
			"}"
			"if (" UNI_ENABLE_CHECKERS ") {"
				"color = check_pattern(color);"
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
	context->uni = (struct gl_uni) {
		.matrix = glGetUniformLocation(program, UNI_MATRIX),
		.color_mode = glGetUniformLocation(program, UNI_COLOR_MODE),
		.checkers = glGetUniformLocation(program, UNI_ENABLE_CHECKERS),
	};
	const GLint samps[] = {
		glGetUniformLocation(program, UNI_IMG),
		glGetUniformLocation(program, UNI_PAL),
		glGetUniformLocation(program, UNI_PLANE3),
		glGetUniformLocation(program, UNI_PLANE4),
	};
	if (context->uni.matrix == -1 || context->uni.color_mode == -1
	|| context->uni.checkers == -1) {
		return false;
	}
	for (size_t i = 0; i < ARRAY_LEN(samps); ++i) {
		if (samps[i] == -1) {
			return false;
		}
	}

	// Error-free from now on.
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

	for (size_t i = 0; i < ARRAY_LEN(samps); ++i) {
		setup_texture((GLint)i, texs, samps);
	}
	glActiveTexture(gl_tex_img);


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

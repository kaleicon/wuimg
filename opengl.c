#include <stdio.h>
#include <math.h>
#include <limits.h>

#include <epoxy/egl.h>

#include "wudefs.h"
#include "common.h"
#include "opengl.h"

#define LOG_SIZE 512

const GLint MAX_LEVEL = 6;

const char * gl_error_str(GLenum error) {
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
	default:
		return "???";
	}
}

void delete_gl_context(const struct gl_context *context) {
	glDeleteTextures(ARRAY_LEN(context->textures), context->textures);
	GLuint obj;

	glGetIntegerv(GL_ARRAY_BUFFER_BINDING, (GLint *)&obj);
	glDeleteBuffers(1, &obj);

	glGetIntegerv(GL_VERTEX_ARRAY_BINDING, (GLint *)&obj);
	glDeleteVertexArrays(1, &obj);
}

void set_gl_alpha(struct gl_context *context, const bool checkers) {
	glUniform1i(context->uni.checkers, checkers);
}

static float fix_aspect_ratio(GLfloat *mat, const struct gl_context *context,
const unsigned char rotation) {
	// Scale the image to its natural size, taking rotation into account.
	const size_t r1 = rotation & 1;
	const float ratio_w = (float)context->tex.w / (float)context->fb.wh[r1];
	const float ratio_h = (float)context->tex.h / (float)context->fb.wh[r1 ^ 1];

	const size_t r2 = 5 - r1;
	mat[r1] *= ratio_w;
	mat[r2] *= ratio_h;
	return 1 / fmaxf(ratio_w, ratio_h);
}

static void set_mirrot(GLfloat *mat, const int rotate, const bool mirror) {
	// Do some complex math to apply both rotation and mirroring
	const GLfloat hard_math[] = {0, 1, 0, -1, 0};

	const GLfloat cosy = hard_math[rotate + 1];
	const GLfloat sinner = hard_math[rotate];
	const GLfloat mirror_mult = (GLfloat)mirror * 2 - 1;

	mat[0] = cosy;
	mat[1] = sinner;
	mat[4] = sinner * -mirror_mult;
	mat[5] = cosy * mirror_mult;
}

void update_gl_matrix(const struct gl_context *context, struct wu_state *state) {
	GLfloat mat[16] = {
		1, 0, 0, 0,
		0, 1, 0, 0,
		0, 0, 1, 0,
		0, 0, 0, 1,
	};

	set_mirrot(mat, state->rotate, state->mirror);
	state->fit_zoom = fix_aspect_ratio(mat, context, state->rotate);

	const float mv_scale = 1.5f;
	mat[12] += state->x_offset / (float)context->fb.w * mv_scale;
	mat[13] += state->y_offset / (float)context->fb.h * mv_scale;
	mat[15] = 1 / state->zoom;

	glUniformMatrix4fv(context->uni.trans, 1, GL_FALSE, mat);
}

float calc_gl_fit_zoom(const struct gl_context *context,
const unsigned char rotation) {
	const size_t r1 = rotation & 1;
	const float ratio_w = (float)context->fb.wh[r1] / (float)context->tex.w;
	const float ratio_h = (float)context->fb.wh[r1 ^ 1] / (float)context->tex.h;
	return fminf(ratio_w, ratio_h);
}

void even_gl_view(struct gl_context *context) {
	/* Slightly resize the viewport so that it always has the same amount
	 * of pixels on opposite sides. */
	const int evener = 0x7ffffffe;
	context->fb.w = (context->fb.w & evener) | (context->tex.w & 1),
	context->fb.h = (context->fb.h & evener) | (context->tex.h & 1),
	glViewport(0, 0, context->fb.w, context->fb.h);
}

static void swizzle_set(const GLenum tex_type, const enum pix_layout layout,
const unsigned char ch) {
	const GLint swizzle_lut[] = {GL_RED, GL_GREEN, GL_BLUE,
		ch % 2 == 0 ? GL_ALPHA : GL_ONE};
	GLint swizzle[4];
	for (size_t i = 0; i < ARRAY_LEN(swizzle); ++i) {
		swizzle[i] = swizzle_lut[(layout >> (6 - i*2)) & 3];
	}
	glTexParameteriv(tex_type, GL_TEXTURE_SWIZZLE_RGBA, swizzle);
}

static GLint set_upload_parameters(const struct raw_img *img,
GLenum *restrict type, GLenum *restrict fmt, struct gl_context *context) {
	glPixelStorei(GL_UNPACK_ALIGNMENT, img->alignment);

	enum pix_layout layout = img->layout;
	if (img->palette) {
		if (img->bitdepth > 8) {
			return 0;
		}

		glActiveTexture(GL_TEXTURE1);
		swizzle_set(GL_TEXTURE_2D, rgba, 4);
		glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, 1, GL_RGBA,
			GL_UNSIGNED_BYTE, img->palette);
		glActiveTexture(GL_TEXTURE0);

		if (!context->tex.paletted) {
			context->tex.paletted = true;
			glUniform1i(context->uni.use_pal, context->tex.paletted);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
				GL_NEAREST);
		}

		swizzle_set(GL_TEXTURE_2D, gray, 1);
		*type = GL_UNSIGNED_BYTE;
		*fmt = GL_RED;
		return GL_R8;
	} else if (context->tex.paletted) {
		context->tex.paletted = false;
		glUniform1i(context->uni.use_pal, context->tex.paletted);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
			GL_LINEAR_MIPMAP_LINEAR);
	}

	GLint in_fmt;
	switch (img->bitdepth) {
	case rgb332:
		if (img->channels != 3) {
			return 0;
		}
		*type = GL_UNSIGNED_BYTE_3_3_2;
		*fmt = GL_RGB;
		in_fmt = GL_R3_G3_B2;
		break;
	case bgra4444:
		if (img->channels != 4) {
			return 0;
		}
		*type = GL_UNSIGNED_SHORT_4_4_4_4_REV;
		*fmt = GL_BGRA;
		in_fmt = GL_RGBA4;

		/* I bruteforced this. I have no clue why it works.
		 * I assume it only works with rgba. */
		layout = (layout << 2) | layout >> 6;
		break;
	case bgra5551:
		if (img->channels != 4) {
			return 0;
		}
		*type = GL_UNSIGNED_SHORT_1_5_5_5_REV;
		*fmt = GL_BGRA;
		in_fmt = GL_RGB5_A1;
		layout = (layout == rgba) ? bgra : rgba;
		break;
	case 8: case 16: case 32:
		;
		const GLenum type_lut[] = {
			GL_UNSIGNED_BYTE,
			img->float_data ? GL_HALF_FLOAT : GL_UNSIGNED_SHORT,
			0,
			img->float_data ? GL_FLOAT : GL_UNSIGNED_INT
		};
		const GLenum fmt_lut[] = {GL_RED, GL_RG, GL_RGB, GL_RGBA};
		const GLint in_fmt_lut[][4] = {
			{GL_R8,    GL_R16,    0, GL_R32F},
			{GL_RG8,   GL_RG16,   0, GL_RG32F},
			{GL_RGB8,  GL_RGB16,  0, GL_RGB32F},
			{GL_RGBA8, GL_RGBA16, 0, GL_RGBA32F}
		};

		*type = type_lut[img->bitdepth / 8 - 1];
		*fmt = fmt_lut[img->channels - 1];
		in_fmt = in_fmt_lut[img->true_channels - 1][img->bitdepth / 8 - 1];
		break;
	default:
		return 0;
	}
	swizzle_set(GL_TEXTURE_2D, layout, img->true_channels);
	return in_fmt;
}

bool load_gl_texture(const struct raw_img *img, struct gl_context *context) {
	GLenum type, fmt;
	const GLint in_fmt = set_upload_parameters(img, &type, &fmt, context);
	if (!in_fmt) {
		printf("Invalid channel/bitdepth combination (%d/%d). Skipping.\n",
			img->channels, img->bitdepth);
		return false;
	}

	glTexImage2D(GL_TEXTURE_2D, 0, in_fmt, (GLsizei)img->w,
		(GLsizei)img->h, 0, fmt, type, img->data);
	const GLenum err = glGetError();
	if (err) {
		printf("Encountered error %x when uploading to texture: %s\n",
			err, gl_error_str(err));
		return false;
	}
	glGenerateMipmap(GL_TEXTURE_2D);
	context->tex.w = (unsigned short)img->w;
	context->tex.h = (unsigned short)img->h;
	context->tex.ch = img->true_channels;
	context->tex.bpp = img->bitdepth;
	return true;
}

bool reuse_gl_texture(const struct raw_img *img, struct gl_context *context) {
	const bool reusable = img->w == context->tex.w
		&& img->h == context->tex.h
		&& img->true_channels == context->tex.ch
		&& img->bitdepth == context->tex.bpp
		&& (bool)img->palette == context->tex.paletted;

	if (reusable) {
		GLenum type, fmt;
		set_upload_parameters(img, &type, &fmt, context);
		glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, (GLsizei)img->w,
			(GLsizei)img->h, fmt, type, img->data);
		glGenerateMipmap(GL_TEXTURE_2D);
	}
	return reusable;
}

void clear_gl_color(const float bg[4]) {
	glClearColor(bg[0], bg[1], bg[2], bg[3]);
}

static void setup_texture(const GLuint texture, const GLenum target,
const GLint tex_location, const GLint idx, const GLint wrap,
const GLint min_filter, const GLint mag_filter, const GLint max_level) {
	const GLenum texture_num[] = {GL_TEXTURE0, GL_TEXTURE1};

	glActiveTexture(texture_num[idx]);
	glBindTexture(target, texture);
	glUniform1i(tex_location, idx);
	glTexParameteri(target, GL_TEXTURE_WRAP_S, wrap);
	glTexParameteri(target, GL_TEXTURE_WRAP_T, wrap);
	glTexParameteri(target, GL_TEXTURE_MIN_FILTER, min_filter);
	glTexParameteri(target, GL_TEXTURE_MAG_FILTER, mag_filter);
	glTexParameteri(target, GL_TEXTURE_MAX_LEVEL, max_level);
}

static bool setup_attributes(const GLuint program, const char *restrict name) {
	GLint al = glGetAttribLocation(program, name);
	if (al >= 0) {
		GLuint attrib = (GLuint)al;
		glVertexAttribPointer(attrib, 2, GL_BYTE, GL_FALSE, 0, 0);
		glEnableVertexAttribArray(attrib);
		return true;
	}
	return false;
}

typedef void (*gl_infolog_func_t)(GLuint, GLsizei, GLsizei *, GLchar *);
typedef void (*gl_getiv_func_t)(GLuint, GLenum, GLint *);

static void check_issues(const gl_infolog_func_t log, const gl_getiv_func_t iv,
const GLuint object, const GLenum parameter, GLchar *logbuf, GLint *status) {
	GLsizei written = 0;
	log(object, LOG_SIZE, &written, logbuf);
	if (written) {
		puts(logbuf);
	}
	iv(object, parameter, status);
}

static GLuint setup_program(const GLuint vertex_shader,
const GLuint fragment_shader, GLchar *logbuf, GLint *status) {
	const GLuint program = glCreateProgram();
	glAttachShader(program, vertex_shader);
	glAttachShader(program, fragment_shader);
	glLinkProgram(program);
	check_issues(glGetProgramInfoLog, glGetProgramiv, program,
		GL_LINK_STATUS, logbuf, status);
	return program;
}

static GLuint setup_shader(const char *shader_code, const GLenum type,
GLchar *logbuf, GLint *status) {
	const GLuint shader = glCreateShader(type);
	glShaderSource(shader, 1, &shader_code, NULL);
	glCompileShader(shader);
	check_issues(glGetShaderInfoLog, glGetShaderiv, shader,
		GL_COMPILE_STATUS, logbuf, status);
	return shader;
}

static void setup_buffer_object(const GLuint buffer_object, const GLenum type,
const GLsizeiptr size, const GLvoid *data, const GLenum usage) {
	glBindBuffer(type, buffer_object);
	glBufferData(type, size, data, usage);
}

bool setup_opengl(struct gl_context *context, struct wu_conf *wuconf) {
/*	printf("vendor: %s\n"
		"renderer: %s\n"
		"version: %s\n"
		"shading: %s\n",
		glGetString(GL_VENDOR), glGetString(GL_RENDERER),
		glGetString(GL_VERSION), glGetString(GL_SHADING_LANGUAGE_VERSION));
*/

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	if (!wuconf->bg_src) {
		GLfloat bg[4];
		for (size_t i = 0; i < sizeof(wuconf->bg); ++i) {
			bg[i] = (float)wuconf->bg[i] / (float)UCHAR_MAX;
		}
		glClearColor(bg[0], bg[1], bg[2], bg[3]);
	}

	GLint mts;
	glGetIntegerv(GL_MAX_TEXTURE_SIZE, &mts);
	if (wuconf->max_img_size) {
		wuconf->max_img_size = (unsigned short)imin(
			wuconf->max_img_size, mts);
	} else {
		wuconf->max_img_size = (unsigned short)mts;
	}

	GLuint vertex_array;
	glGenVertexArrays(1, &vertex_array);
	glBindVertexArray(vertex_array);

	GLbyte vertices[] = {
		-1,-1,  1,-1,

		-1, 1,  1, 1,
//		0, 0,  1, 0,

//		0, 1,  1, 1,
	};
	GLuint array_buf;
	glGenBuffers(1, &array_buf);
	setup_buffer_object(array_buf, GL_ARRAY_BUFFER, sizeof(vertices),
		vertices, GL_STATIC_DRAW);

	const char *vs =
		"#version 150\n"
		"in vec2 pos;"
		"out vec2 texcoord;"
		"uniform mat4 trans;"
		"void main() {"
			"texcoord = pos * vec2(0.5, 0.5) + vec2(0.5, 0.5);"
			"gl_Position = trans * vec4(pos, 0, 1);"
		"}";
	const char *fs =
		"#version 150\n"
		"in vec2 texcoord;"
		"out vec4 color;"
		"uniform sampler2D img;"
		"uniform sampler2D pal;"
		"uniform bool use_palette;"
		"uniform bool checker_alpha;"
		"vec3 gen_check_pattern() {"
			"vec2 abspos = floor(textureSize(img, 5) * texcoord);"
			"float checker = clamp(mod(abspos.x + abspos.y, 2), 0.6, 0.8);"
			"return vec3(color.a) * color.rgb"
				"+ vec3(1 - color.a) * vec3(checker);"
		"}"
		"void main() {"
			"if (use_palette) {"
				"vec4 idx = texture2D(img, texcoord);"
				"color = texture2D(pal, vec2(idx.r, 0));"
			"} else {"
				"color = texture2D(img, texcoord);"
			"}"
			"if (checker_alpha) {"
				"color = vec4(gen_check_pattern(), 1);"
			"}"
		"}";

	GLint status;
	GLchar logbuf[LOG_SIZE];
	const GLuint vertex_shader = setup_shader(vs, GL_VERTEX_SHADER, logbuf,
		&status);
	if (status == GL_FALSE) {
		return false;
	}
	const GLuint fragment_shader = setup_shader(fs, GL_FRAGMENT_SHADER,
		logbuf, &status);
	if (status == GL_FALSE) {
		return false;
	}

	const GLuint program = setup_program(vertex_shader, fragment_shader,
		logbuf, &status);
	if (status == GL_FALSE) {
		return false;
	}
	glUseProgram(program);

	if (!setup_attributes(program, "pos")) {
		return false;
	}

	context->uni.trans = glGetUniformLocation(program, "trans");
	context->uni.use_pal = glGetUniformLocation(program, "use_palette");
	context->uni.checkers = glGetUniformLocation(program, "checker_alpha");
	const GLint pal_samp = glGetUniformLocation(program, "pal");
	const GLint img_samp = glGetUniformLocation(program, "img");
	if (context->uni.trans == -1 || context->uni.use_pal == -1
	|| pal_samp == -1 || img_samp == -1) {
		return false;
	}
	glDeleteShader(fragment_shader);
	glDeleteShader(vertex_shader);
	glDeleteProgram(program);

	glGenTextures(ARRAY_LEN(context->textures), context->textures);
	/* Do mind the 'GL_CLAMP_TO_EDGE' here, paletted images won't work
	 * without it. */
	setup_texture(context->texture.pal, GL_TEXTURE_2D, pal_samp, 1,
		GL_CLAMP_TO_EDGE, GL_NEAREST, GL_NEAREST, 0);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 256, 1, 0, GL_RGBA,
		GL_UNSIGNED_BYTE, NULL);

	setup_texture(context->texture.img, GL_TEXTURE_2D, img_samp, 0,
		GL_CLAMP_TO_BORDER, GL_LINEAR_MIPMAP_LINEAR, GL_NEAREST,
		MAX_LEVEL);

	context->tex.w = 0;
	context->tex.h = 0;
	context->tex.ch = 0;
	context->tex.bpp = 0;
	context->tex.paletted = false;
	return true;
}

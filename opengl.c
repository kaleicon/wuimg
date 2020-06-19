#include <stdio.h>
#include <string.h>
#include <math.h>
#include <limits.h>
#include <assert.h>

#include <epoxy/gl.h>

#include "wudefs.h"
#include "common.h"
#include "opengl.h"

#define LOG_SIZE 512

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
	glDeleteProgram(context->program);
	glDeleteShader(context->fragment_shader);
	glDeleteShader(context->vertex_shader);
	glDeleteBuffers(1, &context->vertex);
	glDeleteVertexArrays(1, &context->vertex_array);
}

void update_gl_matrix(const struct gl_context *context) {
	glUniformMatrix4fv(context->trans_uni, 1, GL_FALSE, context->mat);
}

float correct_gl_aspect_ratio(struct gl_context *context,
const unsigned char rotation) {
	/* Modify the matrix to maintain the zoom factor, taking rotation into
	 * account. */
	const int r1 = rotation & 1;
	const float ratio_w = (float)context->tex_w / (float)context->fb_wh[r1];
	const float ratio_h = (float)context->tex_h / (float)context->fb_wh[r1 ^ 1];

	const int r2 = 5 - r1;
	context->mat[r1] = copysignf(ratio_w, context->mat[r1]);
	context->mat[r2] = copysignf(ratio_h, context->mat[r2]);
	return 1 / fmaxf(ratio_w, ratio_h);
}

void even_gl_view(struct gl_context *context) {
	/* Resize the viewport such that it always has the same amount of
	 * pixels on opposite sides. */
	const int evener = 0x7ffffffe;
	context->fb_w = (context->fb_w & evener) | (context->tex_w & 1),
	context->fb_h = (context->fb_h & evener) | (context->tex_h & 1),
	glViewport(0, 0, context->fb_wh[0], context->fb_wh[1]);
}

void calc_gl_mirrot(struct gl_context *context, const struct wu_state *state) {
	/* Apply the rotation and mirroring set in wu_state */
	const GLfloat hard_math[] = {0, 1, 0, -1, 0};

	const GLfloat cosy = hard_math[state->rotate + 1];
	const GLfloat sinner = hard_math[state->rotate];
	const GLfloat mirror_mult = (GLfloat)state->mirror * 2 - 1;

	context->mat[0] = cosy;
	context->mat[1] = sinner;
	context->mat[4] = sinner * mirror_mult;
	context->mat[5] = cosy * -mirror_mult;
}

void change_gl_rotation(struct gl_context *context, struct wu_state *state,
const int turns) {
	state->rotate = (unsigned char)iwrapadd(state->rotate, turns, 4);
	calc_gl_mirrot(context, state);
}

void change_gl_offset(struct gl_context *context, const bool relative,
const float x, const float y) {
	if (relative) {
		context->trans.offset_x += x * context->trans.scale;
		context->trans.offset_y += y * context->trans.scale;
	} else {
		context->trans.offset_x = x;
		context->trans.offset_y = y;
	}
}

void set_gl_scaling(struct gl_context *context, struct wu_state *state,
const float new_zoom) {
	state->zoom = fclampf(new_zoom, MIN_ZOOM, MAX_ZOOM);
	context->trans.scale = 1 / state->zoom;
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
		assert(img->bitdepth <= 8);
		const GLsizei pal_len = 1 << img->bitdepth;

		glActiveTexture(GL_TEXTURE1);
		swizzle_set(GL_TEXTURE_1D, layout, img->channels);
		glTexSubImage1D(GL_TEXTURE_1D, 0, 0, pal_len, GL_RGBA,
			GL_UNSIGNED_BYTE, img->palette);
		glActiveTexture(GL_TEXTURE0);

		if (!context->use_palette) {
			context->use_palette = true;
			glUniform1i(context->use_pal_uni, context->use_palette);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
				GL_NEAREST);
		}

		swizzle_set(GL_TEXTURE_2D, gray, 1);
		*type = GL_UNSIGNED_BYTE;
		*fmt = GL_RED;
		return GL_R8;
	} else if (context->use_palette) {
		context->use_palette = false;
		glUniform1i(context->use_pal_uni, context->use_palette);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
			GL_LINEAR_MIPMAP_LINEAR);
	}

	GLint in_fmt;
	if (img->bitdepth == 4) {
		assert(img->channels == 4);
		*type = GL_UNSIGNED_SHORT_4_4_4_4_REV;
		*fmt = GL_BGRA;
		in_fmt = GL_RGBA4;

		/* I bruteforced this. I have absolutely no clue why it works.
		 * I assume it only works with rgba. */
		layout = (layout << 2) | layout >> 6;
	} else if (img->bitdepth == 3) {
		assert(img->channels == 3);
		*type = GL_UNSIGNED_BYTE_3_3_2;
		*fmt = GL_RGB;
		in_fmt = GL_R3_G3_B2;
	} else if (img->bitdepth == 5) {
		assert(img->channels == 4);
		*type = GL_UNSIGNED_SHORT_1_5_5_5_REV;
		*fmt = GL_BGRA;
		in_fmt = GL_RGB5_A1;
		layout = layout == rgba ? bgra : rgba;
	} else {
		const GLenum type_lut[] = {
			GL_UNSIGNED_BYTE, GL_UNSIGNED_SHORT, 0, GL_UNSIGNED_INT
		};
		const GLenum fmt_lut[] = {GL_RED, GL_RG, GL_RGB, GL_RGBA};
		const GLint in_fmt_lut[][4] = {
			{GL_R8,    GL_R16},
			{GL_RG8,   GL_RG16},
			{GL_RGB8,  GL_RGB16},
			{GL_RGBA8, GL_RGBA16}
		};

		const int bitdepth = imax(img->bitdepth, 16);

		*type = type_lut[img->bitdepth / 8 - 1];
		*fmt = fmt_lut[img->channels - 1];
		in_fmt = in_fmt_lut[img->true_channels - 1][bitdepth / 8 - 1];
	}
	swizzle_set(GL_TEXTURE_2D, layout, img->true_channels);
	return in_fmt;
}

bool load_gl_texture(const struct raw_img *img, struct gl_context *context) {
	GLenum type, fmt;
	const GLint in_fmt = set_upload_parameters(img, &type, &fmt, context);

	glTexImage2D(GL_TEXTURE_2D, 0, in_fmt, (GLsizei)img->w,
		(GLsizei)img->h, 0, fmt, type, img->data);
	const GLenum err = glGetError();
	if (err) {
		printf("Encountered error %x when uploading to texture: %s\n",
			err, gl_error_str(err));
		return false;
	}

	glGenerateMipmap(GL_TEXTURE_2D);
	context->tex_w = (unsigned short)img->w;
	context->tex_h = (unsigned short)img->h;
	context->tex_ch = img->true_channels;
	context->tex_bpp = img->bitdepth;
	return true;
}

bool reuse_gl_texture(const struct raw_img *img, struct gl_context *context) {
	if (img->w != context->tex_w || img->h != context->tex_h
	|| img->true_channels != context->tex_ch
	|| img->bitdepth != context->tex_bpp || img->dec_scale != 1
	|| (bool)img->palette != context->use_palette) {
		return false;
	}

	GLenum type, fmt;
	set_upload_parameters(img, &type, &fmt, context);
	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, (GLsizei)img->w,
		(GLsizei)img->h, fmt, type, img->data);
	glGenerateMipmap(GL_TEXTURE_2D);
	return true;
}

void clear_gl_color(const unsigned char fallback_bg[4],
const unsigned char img_bg[4], const enum background_source source) {
	const float max = (float)UCHAR_MAX;
	float bg[4];

	if (img_bg && source != default_only && img_bg[3]) {
		if (source == image_rgb) {
			bg[3] = fallback_bg[3] / max;
			const float scale = img_bg[3] / max * bg[3];
			for (int i = 0; i < 3; ++i) {
				bg[i] = img_bg[i] / max * scale;
			}
		} else {
			for (int i = 0; i < 4; ++i) {
				bg[i] = img_bg[i] / max;
			}
		}
	} else {
		for (int i = 0; i < 4; ++i) {
			bg[i] = fallback_bg[i] / max;
		}
	}
	glClearColor(bg[0], bg[1], bg[2], bg[3]);
}

static void reset_gl_matrix(GLfloat mat[16]) {
	const GLfloat identity[16] = {
		1, 0, 0, 0,
		0, 1, 0, 0,
		0, 0, 1, 0,
		0, 0, 0, 1,
	};
	memcpy(mat, identity, sizeof(identity));
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
		glVertexAttribPointer(attrib, 2, GL_FLOAT, GL_FALSE, 0, 0);
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

static GLuint setup_program(const GLuint fragment_shader,
const GLuint vertex_shader, GLchar *logbuf, GLint *status) {
	const GLuint program = glCreateProgram();
	glAttachShader(program, fragment_shader);
	glAttachShader(program, vertex_shader);
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
const GLsizeiptr size, const GLvoid *data) {
	glBindBuffer(type, buffer_object);
	glBufferData(type, size, data, GL_STATIC_DRAW);
}

bool setup_opengl(struct gl_context *context, struct wu_conf *wuconf) {
/*	printf("vendor: %s\n"
		"renderer: %s\n"
		"version: %s\n"
		"shading: %s\n",
		glGetString(GL_VENDOR), glGetString(GL_RENDERER),
		glGetString(GL_VERSION), glGetString(GL_SHADING_LANGUAGE_VERSION));
*/
	GLint major, minor;
	glGetIntegerv(GL_MAJOR_VERSION, &major);
	glGetIntegerv(GL_MINOR_VERSION, &minor);
	const GLint version = major * 10 + minor;

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	clear_gl_color(wuconf->bg, NULL, default_only);
	if (!wuconf->ignore_tex_limit) {
		GLint mts;
		glGetIntegerv(GL_MAX_TEXTURE_SIZE, &mts);
		if (wuconf->max_img_size) {
			wuconf->max_img_size = umin(wuconf->max_img_size,
				(unsigned int)mts);
		} else {
			wuconf->max_img_size = (unsigned int)mts;
		}
	} else if (!wuconf->max_img_size) {
		wuconf->max_img_size = UINT_MAX;
	}

	glGenVertexArrays(1, &context->vertex_array);
	glBindVertexArray(context->vertex_array);

	GLfloat vertices[] = {
/*		-1,-3,
		-1, 1,  3, 1,*/
		-1,-1,  1,-1,

		-1, 1,  1, 1,
	};
	glGenBuffers(1, &context->vertex);
	setup_buffer_object(context->vertex, GL_ARRAY_BUFFER, sizeof(vertices),
		vertices);

	const char *vs =
		"#version 150\n"
		"in vec2 pos;"
		"out vec2 texcoord;"
		"uniform mat4 trans;"
		"void main() {"
			"texcoord = pos * vec2(0.5, -0.5) + 0.5;"
			"gl_Position = trans * vec4(pos, 0, 1);"
		"}";
	const char *fs =
		"#version 150\n"
		"in vec2 texcoord;"
		"out vec4 color;"
		"uniform sampler2D img;"
		"uniform sampler1D pal;"
		"uniform bool use_palette;"
		"void main() {"
			"if (use_palette) {"
				"vec4 idx = texture(img, texcoord);"
				"color = texture(pal, idx.r);"
			"} else {"
				"color = texture(img, texcoord);"
			"}"
		"}";

	GLint status;
	GLchar logbuf[LOG_SIZE];
	context->vertex_shader = setup_shader(vs, GL_VERTEX_SHADER, logbuf,
		&status);
	if (status == GL_FALSE) {
		return false;
	}
	context->fragment_shader = setup_shader(fs, GL_FRAGMENT_SHADER, logbuf,
		&status);
	if (status == GL_FALSE) {
		return false;
	}

	context->program = setup_program(context->fragment_shader,
		context->vertex_shader, logbuf, &status);
	if (status == GL_FALSE) {
		return false;
	}
	glUseProgram(context->program);

	if (!setup_attributes(context->program, "pos")) {
		return false;
	}

	context->trans_uni = glGetUniformLocation(context->program, "trans");
	context->use_pal_uni = glGetUniformLocation(context->program, "use_palette");
	const GLint pal_samp = glGetUniformLocation(context->program, "pal");
	const GLint img_samp = glGetUniformLocation(context->program, "img");
	if (context->trans_uni == -1 || context->use_pal_uni == -1
	|| pal_samp == -1 || img_samp == -1) {
		return false;
	}

	glGenTextures(ARRAY_LEN(context->textures), context->textures);
	// Please mind the 'GL_CLAMP_TO_EDGE' here.
	setup_texture(context->texture.pal, GL_TEXTURE_1D, pal_samp, 1,
		GL_CLAMP_TO_EDGE, GL_NEAREST, GL_NEAREST, 0);
	if (version >= 42) {
		glTexStorage1D(GL_TEXTURE_1D, 1, GL_RGBA8, 256);
	} else {
		glTexImage1D(GL_TEXTURE_1D, 0, GL_RGBA8, 256, 0, GL_RGBA,
			GL_UNSIGNED_BYTE, NULL);
	}
	setup_texture(context->texture.img, GL_TEXTURE_2D, img_samp, 0,
		GL_CLAMP_TO_BORDER, GL_LINEAR_MIPMAP_LINEAR, GL_NEAREST, 6);

	context->tex_w = 0;
	context->tex_h = 0;
	context->tex_ch = 0;
	context->tex_bpp = 0;
	context->use_palette = false;

	glUniform1i(context->use_pal_uni, context->use_palette);
	reset_gl_matrix(context->mat);
	return true;
}

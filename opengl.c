#include <stdio.h>
#include <string.h>
#include <math.h>

#include <epoxy/gl.h>

#include "wudefs.h"
#include "common.h"
#include "opengl.h"

#define LOG_SIZE 192

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
	glDeleteTextures(1, &context->texture);
	glDeleteProgram(context->program);
	glDeleteShader(context->fragment_shader);
	glDeleteShader(context->vertex_shader);
	glDeleteBuffers(1, &context->element_buf);
	glDeleteBuffers(1, &context->vertex_buf);
	glDeleteVertexArrays(1, &context->vertex_array);
}

void update_gl_matrix(const struct gl_context *context) {
	glUniformMatrix4fv(context->trans_uni, 1, GL_FALSE, context->mat);
}

void calc_gl_mirrot(struct gl_context *context) {
	const GLfloat hard_math[] = {0, 1, 0, -1, 0};

	const GLfloat cosy = hard_math[context->rotate + 1];
	const GLfloat sinner = hard_math[context->rotate];
	const GLfloat mirror_mult = (GLfloat)context->mirror * 2 - 1;

	context->mat[0] = cosy;
	context->mat[1] = sinner;
	context->mat[4] = sinner * mirror_mult;
	context->mat[5] = cosy * -mirror_mult;
}

void change_gl_rotation(struct gl_context *context, const int turns) {
	context->rotate = (unsigned char)iwrapadd(context->rotate, turns, 4);
	calc_gl_mirrot(context);
}

void reset_gl_offset(struct gl_context *context) {
	context->trans.offset_x = 0;
	context->trans.offset_y = 0;
}

void change_gl_offset(struct gl_context *context, const float x, const float y) {
	context->trans.offset_x += x * context->trans.scale;
	context->trans.offset_y += y * context->trans.scale;
	update_gl_matrix(context);
}

void set_gl_scaling(struct gl_context *context, const float zoom) {
	context->trans.scale = fclampf(zoom, MIN_ZOOM, MAX_ZOOM);
	update_gl_matrix(context);
}

void correct_gl_view(struct gl_context *context, const int fb_w,
const int fb_h) {
	const int evener = 0x7ffffffe;
	const int fb[2] = {
		(fb_w & evener) | (int)(context->tex_w & 1),
		(fb_h & evener) | (int)(context->tex_h & 1),
	};
	glViewport(0, 0, fb[0], fb[1]);

	const int r1 = context->rotate & 1;
	const int r2 = 5 - r1;
	const float ratio_w = (float)context->tex_w / (float)fb[r1];
	const float ratio_h = (float)context->tex_h / (float)fb[r1 ^ 1];

	context->fit_zoom = fmaxf(ratio_w, ratio_h);
	context->mat[r1] = copysignf(ratio_w, context->mat[r1]);
	context->mat[r2] = copysignf(ratio_h, context->mat[r2]);
}

void reset_gl_matrix(struct gl_context *context) {
	const float identity[16] = {
		1, 0, 0, 0,
		0, 1, 0, 0,
		0, 0, 1, 0,
		0, 0, 0, 1,
	};
	memcpy(context->mat, identity, sizeof(identity));
}

static GLint calc_texture_parameters(const struct raw_img *img,
GLenum *restrict type, GLenum *restrict fmt) {
	GLenum type_lut[] = {GL_UNSIGNED_BYTE, GL_UNSIGNED_SHORT};
	GLenum fmt_lut[] = {GL_RED, GL_RG, GL_RGB, GL_RGBA};
	GLint in_fmt_lut[][8] = {
		//2bit(1)     4bit(2)  (3) 8bit(4)  (5) 12bit(6)  (7) 16bit(8)
		{0,           0,        0, GL_R8,    0, 0,         0, GL_R16},
		{0,           0,        0, GL_RG8,   0, 0,         0, GL_RG16},
		{GL_R3_G3_B2, GL_RGB4,  0, GL_RGB8,  0, GL_RGB12,  0, GL_RGB16},
		{GL_RGBA2,    GL_RGBA4, 0, GL_RGBA8, 0, GL_RGBA12, 0, GL_RGBA16}
	};

	*type = type_lut[img->bitdepth / 8 - 1];
	*fmt = fmt_lut[img->channels - 1];
	return in_fmt_lut[img->true_channels - 1][img->true_bitdepth/2 - 1];
}

void update_gl_texture(const struct raw_img *img) {
	GLenum type, fmt;
	calc_texture_parameters(img, &type, &fmt);
	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, (GLsizei)img->w,
		(GLsizei)img->h, fmt, type, img->data);
	glGenerateMipmap(GL_TEXTURE_2D);
	return;
}

bool load_gl_texture(const struct raw_img *img, struct gl_context *context) {
	if (img->w > context->max_tex_size && img->h > context->max_tex_size) {
		return false;
	}

	GLenum type, fmt;
	GLint in_fmt = calc_texture_parameters(img, &type, &fmt);
	if (!in_fmt) {
		puts(HIGHLIGHT "The unthinkable has happened, in_fmt == 0" RESET);
		return false;
	}

	glTexImage2D(GL_TEXTURE_2D, 0, in_fmt, (GLsizei)img->w,
		(GLsizei)img->h, 0, fmt, type, img->data);
	glGenerateMipmap(GL_TEXTURE_2D);
	context->tex_w = img->w;
	context->tex_h = img->h;
	context->tex_ch = img->true_channels;
	context->tex_bpp = img->true_bitdepth;

	GLint rgba[] = {GL_RED, GL_GREEN, GL_BLUE, GL_ALPHA};
	if (img->true_channels < 3) {
		rgba[1] = GL_RED;
		rgba[2] = GL_RED;
	}
	glTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_RGBA, rgba);
	return true;
}

bool is_texture_reusable(const struct raw_img *img,
const struct gl_context *context) {
	return (img->w == context->tex_w && img->h == context->tex_h
	&& img->true_channels == context->tex_ch
	&& img->true_bitdepth == context->tex_bpp);
}

static GLuint setup_texture(const GLint wrap, const GLint min_filter,
const GLint mag_filter) {
	GLuint texture;
	glGenTextures(1, &texture);
	glBindTexture(GL_TEXTURE_2D, texture);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, min_filter);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, mag_filter);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 8);
	return texture;
}

static bool setup_attributes(const GLuint program, const char *restrict name) {
	GLint _a = glGetAttribLocation(program, name);
	if (_a >= 0) {
		GLuint attrib = (GLuint)_a;
		glVertexAttribPointer(attrib, 2, GL_FLOAT, GL_FALSE, 0, 0);
		glEnableVertexAttribArray(attrib);
		return true;
	}
	return false;
}

static void check_issues(void (*log)(GLuint, GLsizei, GLsizei *, GLchar *),
void (*iv)(GLuint, GLenum, GLint *), const GLuint shader,
const GLenum parameter, GLchar *logbuf, GLint *status) {
	GLsizei written = 0;
	log(shader, LOG_SIZE, &written, logbuf);
	if (written) {
		puts(logbuf);
	}
	iv(shader, parameter, status);
}

static GLuint setup_shader(const char *shader_code, const GLenum type,
GLchar *logbuf, GLint *status) {
	GLuint shader = glCreateShader(type);
	glShaderSource(shader, 1, &shader_code, NULL);
	glCompileShader(shader);
	check_issues(glGetShaderInfoLog, glGetShaderiv, shader,
		GL_COMPILE_STATUS, logbuf, status);
	return shader;
}

static GLuint setup_buffer_object(const GLenum type, const GLsizeiptr size,
const GLvoid *data) {
	GLuint bo;
	glGenBuffers(1, &bo);
	glBindBuffer(type, bo);
	glBufferData(type, size, data, GL_STATIC_DRAW);
	return bo;
}

bool setup_opengl(struct gl_context *context) {
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glPixelStorei(GL_UNPACK_SWAP_BYTES, 1);
	glHint(GL_LINE_SMOOTH_HINT, GL_FASTEST);
	glHint(GL_POLYGON_SMOOTH_HINT, GL_FASTEST);

	GLint mts;
	glGetIntegerv(GL_MAX_TEXTURE_SIZE, &mts);
	context->max_tex_size = (unsigned int)mts;
	context->tex_w = 0;
	context->tex_h = 0;
	context->tex_ch = 0;
	context->tex_bpp = 0;

	glGenVertexArrays(1, &context->vertex_array);
	glBindVertexArray(context->vertex_array);

	GLfloat vertices[] = {
		-1,-1,  1,-1,

		-1, 1,  1, 1,
	};
	GLubyte elements[] = {
		0, 1, 2,
		3, 2, 1,
	};
	context->vertex_buf = setup_buffer_object(GL_ARRAY_BUFFER,
		sizeof(vertices), vertices);
	context->element_buf = setup_buffer_object(GL_ELEMENT_ARRAY_BUFFER,
		sizeof(elements), elements);

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
		"void main() {"
			"color = texture(img, texcoord);"
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

	context->program = glCreateProgram();
	glAttachShader(context->program, context->fragment_shader);
	glAttachShader(context->program, context->vertex_shader);
	glLinkProgram(context->program);
	check_issues(glGetProgramInfoLog, glGetProgramiv, context->program,
		GL_LINK_STATUS, logbuf, &status);
	if (status == GL_FALSE) {
		return false;
	}
	glUseProgram(context->program);

	if (!setup_attributes(context->program, "pos")) {
		return false;
	}

	context->trans_uni = glGetUniformLocation(context->program, "trans");
	if (context->trans_uni == -1) {
		return false;
	}

	context->texture = setup_texture(GL_CLAMP_TO_BORDER,
		GL_LINEAR_MIPMAP_LINEAR, GL_NEAREST);
	return true;
}

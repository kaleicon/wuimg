#include <stdio.h>

#include <epoxy/gl.h>

#include "wudefs.h"
#include "common.h"
#include "opengl.h"

#define LOG_SIZE 160

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

void update_gl_scaling(const struct gl_context *context) {
	glUniform2f(context->scale_uni,
		context->normal_x * context->zoom,
		context->normal_y * context->zoom);
}

void normalize_gl_viewport(struct gl_context *context, int fb_w, int fb_h) {
	const int evener = 0x7ffffffe;
	fb_w = (fb_w & evener) | (int)(context->tex_w & 1);
	fb_h = (fb_h & evener) | (int)(context->tex_h & 1);

	context->normal_x = (float)context->tex_w / (float)fb_w;
	context->normal_y = (float)context->tex_h / (float)fb_h;
	context->fit_zoom = 1 / fmaxf(context->normal_x, context->normal_y);
	glViewport(0, 0, fb_w, fb_h);
}

#define DEFAULT_SCALE 1
#define DEFAULT_OFFSET 0
void reset_gl_draw_state(struct gl_context *context) {
	context->zoom = DEFAULT_SCALE;
	context->fit_zoom = DEFAULT_SCALE;
	context->normal_x = DEFAULT_SCALE;
	context->normal_y = DEFAULT_SCALE;
	context->offset_x = DEFAULT_OFFSET;
	context->offset_y = DEFAULT_OFFSET;
	glUniform2f(context->scale_uni, DEFAULT_SCALE, DEFAULT_SCALE);
	glUniform2f(context->offset_uni, DEFAULT_OFFSET, DEFAULT_OFFSET);
}

static GLint calc_texture_parameters(const struct raw_img *img, GLenum *type,
GLenum *fmt) {
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
	return in_fmt_lut[img->true_channels-1][img->true_bitdepth/2-1];
}

bool update_gl_texture(const struct raw_img *img, struct gl_context *context) {
	if (context->tex_w == img->w && context->tex_h == img->h
	&& context->tex_ch == img->channels && context->tex_bpp == img->bitdepth) {
		GLenum type, fmt;
		calc_texture_parameters(img, &type, &fmt);
		glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, (GLsizei)img->w,
			(GLsizei)img->h, fmt, type, img->data);
		glGenerateMipmap(GL_TEXTURE_2D);
		return true;
	}
	return false;
}

bool load_gl_texture(const struct raw_img *img, struct gl_context *context) {
	if (img->w > context->max_tex_size && img->h > context->max_tex_size) {
		return false;
	}

	GLenum type, fmt;
	GLint in_fmt = calc_texture_parameters(img, &type, &fmt);

	if (in_fmt == 0) {
		printf("CHECKME: in_fmt == 0 in load_gl_texture()\n"
			"channels: %u, bit depth: %u\n",
			img->true_channels, img->true_bitdepth);
		in_fmt = GL_R8;
	}

	glTexImage2D(GL_TEXTURE_2D, 0, in_fmt, (GLsizei)img->w,
		(GLsizei)img->h, 0, fmt, type, img->data);
	glGenerateMipmap(GL_TEXTURE_2D);
	context->tex_w = img->w;
	context->tex_h = img->h;
	context->tex_ch = img->channels;
	context->tex_bpp = img->bitdepth;

	GLint rgba[] = {GL_RED, GL_GREEN, GL_BLUE, GL_ALPHA};
	if (img->true_channels <= 2) {
		rgba[1] = GL_RED;
		rgba[2] = GL_RED;
	}
	glTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_RGBA, rgba);
	return true;
}

static GLuint setup_texture(GLint wrap, GLint min_filter, GLint mag_filter) {
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

static bool setup_attributes(GLuint program, const char *restrict name) {
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
void (*iv)(GLuint, GLenum, GLint *), GLuint shader, GLenum parameter,
GLchar *logbuf, GLint *status) {
	GLsizei written = 0;
	log(shader, LOG_SIZE, &written, logbuf);
	if (written) {
		puts(logbuf);
	}
	iv(shader, parameter, status);
}

static GLuint setup_shader(const char *shader_code, GLenum type, GLchar *logbuf,
GLint *status) {
	GLuint shader = glCreateShader(type);
	glShaderSource(shader, 1, &shader_code, NULL);
	glCompileShader(shader);
	check_issues(glGetShaderInfoLog, glGetShaderiv, shader,
		GL_COMPILE_STATUS, logbuf, status);
	return shader;
}

static GLuint setup_buffer_object(GLenum type, GLsizeiptr size,
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
	GLint dims[2];
	glGetIntegerv(GL_MAX_TEXTURE_SIZE, &mts);
	glGetIntegerv(GL_MAX_VIEWPORT_DIMS, dims);
	printf("GL_MAX_TEXTURE_SIZE: %d\n"
		"GL_MAX_VIEWPORT_DIMS: %dx%d\n",
		mts, dims[0], dims[1]);
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
		"uniform vec2 scale;"
		"uniform vec2 offset;"
		"void main() {"
			"texcoord = pos * vec2(0.5, -0.5) + 0.5;"
			"gl_Position = vec4((pos + offset) * scale, 0, 1);"
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

	context->scale_uni = glGetUniformLocation(context->program, "scale");
	if (context->scale_uni == -1) {
		return false;
	}
	context->offset_uni = glGetUniformLocation(context->program, "offset");
	if (context->offset_uni == -1) {
		return false;
	}

	context->texture = setup_texture(GL_CLAMP_TO_BORDER,
		GL_LINEAR_MIPMAP_LINEAR, GL_NEAREST);
	return true;
}

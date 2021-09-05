#include <stdio.h>
#include <math.h>
#include <limits.h>
#include <string.h>

#include <epoxy/gl.h>

#include "wudefs.h"
#include "common.h"
#include "opengl.h"
#include "raster/unpack.h"

static const GLsizei LOG_SIZE = 512;

struct gl_upload_params {
	GLenum type, fmt;
	GLint in_fmt;
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
	default:
		return "???";
	}
}

void gl_context_delete(struct gl_context *context) {
	GLint texs[2];
	glGetIntegerv(GL_TEXTURE_BINDING_2D, texs);
	glActiveTexture(GL_TEXTURE1);
	glGetIntegerv(GL_TEXTURE_BINDING_2D, texs + 1);
	glDeleteTextures(ARRAY_LEN(texs), (GLuint *)texs);

	GLint obj[1];
	glGetIntegerv(GL_ARRAY_BUFFER_BINDING, obj);
	glDeleteBuffers(1, (GLuint *)obj);

	glGetIntegerv(GL_VERTEX_ARRAY_BINDING, obj);
	glDeleteVertexArrays(1, (GLuint *)obj);

	glDeleteQueries(1, &context->timer);
}

GLuint64 gl_clock_end(struct gl_context *context) {
	glEndQuery(GL_TIME_ELAPSED);
	GLuint64 ns = 0;
	glGetQueryObjectui64v(context->timer, GL_QUERY_RESULT, &ns);
	return ns;
}

void gl_clock_start(struct gl_context *context) {
	glBeginQuery(GL_TIME_ELAPSED, context->timer);
}

void gl_alpha_state(struct gl_context *context, const enum alpha_state alpha) {
	glUniform1i(context->uni.checkers, alpha & 1);
	if (alpha & 2) {
		glDisable(GL_BLEND);
	} else {
		glEnable(GL_BLEND);
	}
}

static float fix_aspect_ratio(GLfloat *mat, const struct gl_context *context,
const unsigned char rotation) {
	// Scale the image to its natural size, taking rotation into account.
	const int r1 = rotation & 1;
	const float ratio_w = (float)context->tex.w / (float)context->fb_wh[r1];
	const float ratio_h = (float)context->tex.h / (float)context->fb_wh[r1^1];

	if (mat) {
		const int r2 = 5 - r1;
		mat[r1] *= ratio_w;
		mat[r2] *= ratio_h;
	}
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

	const float mv_scale = 1.5f;
	mat[12] += state->x_offset / (float)context->fb_wh[0] * mv_scale;
	mat[13] += state->y_offset / (float)context->fb_wh[1] * mv_scale;
	mat[15] = 1 / state->zoom;

	glUniformMatrix4fv(context->uni.trans, 1, GL_FALSE, mat);
}

float gl_fit_zoom(const struct gl_context *context,
const unsigned char rotation) {
	return fix_aspect_ratio(NULL, context, rotation);
}

void gl_even_view(struct gl_context *context) {
	/* Shave or add a pixel to the viewport so that it always has the same
	 * amount of pixels on opposite sides. */
	const unsigned evener = UINT_MAX - 1;
	context->fb_wh[0] = (context->fb_wh[0] & evener) | (context->tex.w & 1);
	context->fb_wh[1] = (context->fb_wh[1] & evener) | (context->tex.h & 1);
	glViewport(0, 0, (int)context->fb_wh[0], (int)context->fb_wh[1]);
}

static void swizzle_set(const GLenum tex_type, const enum pix_layout layout,
const bool alpha) {
	const GLint lut[] = {GL_RED, GL_GREEN, GL_BLUE,
		alpha ? GL_ALPHA : GL_ONE};
	GLint swizzle[4];
	for (size_t i = 0; i < ARRAY_LEN(swizzle); ++i) {
		const size_t sh = 6 - i*2;
		swizzle[i] = lut[(layout >> sh) & 3];
	}
	glTexParameteriv(tex_type, GL_TEXTURE_SWIZZLE_RGBA, swizzle);
}

static bool set_upload_parameters(const struct raw_img *img,
struct gl_upload_params *params, struct gl_context *context) {
	enum pix_layout layout = img->layout;
	if (img->palette) {
		if (img->bitdepth > 8) {
			return false;
		}

		glActiveTexture(GL_TEXTURE1);
		glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 1 << img->bitdepth, 1,
			GL_RGBA, GL_UNSIGNED_BYTE, img->palette);
		swizzle_set(GL_TEXTURE_2D, layout, true);
		glActiveTexture(GL_TEXTURE0);
		swizzle_set(GL_TEXTURE_2D, pix_gray, false);

		if (!context->tex.paletted) {
			context->tex.paletted = true;
			glUniform1i(context->uni.use_pal, context->tex.paletted);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
				GL_NEAREST);
		}

		*params = (struct gl_upload_params) {
			.type = GL_UNSIGNED_BYTE,
			.fmt = GL_RED,
			.in_fmt = GL_R8,
			.convert = img->bitdepth < 8,
		};
		return true;
	} else {
		if (context->tex.paletted) {
			context->tex.paletted = false;
			glUniform1i(context->uni.use_pal, context->tex.paletted);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
				GL_LINEAR_MIPMAP_LINEAR);
		}
	}

	if (img->attr == pix_packing_332) {
		*params = (struct gl_upload_params) {
			.type = GL_UNSIGNED_BYTE_3_3_2,
			.fmt = GL_RGB,
			.in_fmt = GL_R3_G3_B2,
			.convert = false,
		};
		swizzle_set(GL_TEXTURE_2D, layout, false);
		return true;
	}

	switch (img->bitdepth) {
	case pix_argb1555:
		if (img->channels < 3) {
			return false;
		}
		*params = (struct gl_upload_params) {
			.type = GL_UNSIGNED_SHORT_1_5_5_5_REV,
			.fmt = GL_BGRA, // For speed
			.in_fmt = GL_RGB5_A1,
			.convert = false,
		};
		layout = (layout == pix_rgba) ? pix_bgra : pix_rgba;
		break;
	case 4:
		if (img->channels == 4) {
			*params = (struct gl_upload_params) {
				.type = GL_UNSIGNED_SHORT_4_4_4_4_REV,
				.fmt = GL_BGRA,
				.in_fmt = GL_RGBA4,
				.convert = false,
			};

			/* I bruteforced this. I have no clue why it works.
			 * I assume it only works with rgba. */
			layout = (layout << 2) | layout >> 6;
			break;
		}
		// fallthrough
	case 1: case 2: case 8: case 16: case 24: case 32: case 64:
		;const bool is_float = img->attr == pix_float;
		const GLenum type_lut[] = {
			GL_UNSIGNED_BYTE,
			is_float ? GL_HALF_FLOAT : GL_UNSIGNED_SHORT,
			GL_UNSIGNED_INT,
			is_float ? GL_FLOAT : GL_UNSIGNED_INT
		};
		const GLenum fmt_lut[] = {GL_RED, GL_RG, GL_RGB, GL_RGBA};

		const int bd = imin((img->bitdepth + 7) / 8 - 1, 3);
		*params = (struct gl_upload_params) {
			.type = type_lut[bd],
			.fmt = fmt_lut[img->channels - 1],
			.in_fmt = (GLint)fmt_lut[img->true_channels - 1],
			.convert = img->bitdepth < 8 || img->bitdepth == 24
				|| img->bitdepth == 64 || img->attr & pix_inverted,
		};
		break;
	default:
		return false;
	}
	swizzle_set(GL_TEXTURE_2D, layout, !(img->true_channels % 2));
	return true;
}

static bool tex_upload(const struct raw_img *img, struct gl_context *context,
const struct gl_upload_params *params, const bool reuse) {
	const void *data;
	if (params->convert) {
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

		const GLsizeiptr bytedepth = img->bitdepth == 24
			? 4 : ((img->bitdepth + 7) / 8);
		const GLsizeiptr size = (GLsizeiptr)(img->w * img->h
			* img->channels) * bytedepth;

		glBindBuffer(GL_PIXEL_UNPACK_BUFFER, context->pixel_unpack_buf);
		glBufferData(GL_PIXEL_UNPACK_BUFFER, size, NULL, GL_STATIC_DRAW);

		enum unpack_op op = op_expand;
		if (img->palette) {
			op = op_unpack;
		} else if (img->bitdepth > 32) {
			op = img->attr & pix_float ? op_pack_float : op_pack;
		} else if (img->attr == pix_inverted) {
			op = op_expand_invert;
		}
		void *map = glMapBuffer(GL_PIXEL_UNPACK_BUFFER, GL_WRITE_ONLY);
		strip_unpack(map, img->data, img->w * img->channels, img->h,
			img->alignment, op, img->bitdepth);
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

	const GLenum err = glGetError();
	if (err) {
		printf("Encountered error %x when uploading to texture: %s\n",
			err, gl_strerror(err));
		return false;
	}

	glGenerateMipmap(GL_TEXTURE_2D);
	return true;
}

bool gl_texture_upload(const struct raw_img *img, struct gl_context *context) {
	struct gl_upload_params params;
	if (!set_upload_parameters(img, &params, context)) {
		printf("Invalid channel/bitdepth combination (%d/%d). Skipping.\n",
			img->channels, img->bitdepth);
		return false;
	}

	const bool success = tex_upload(img, context, &params, false);
	if (success) {
		context->tex.w = (unsigned)img->w;
		context->tex.h = (unsigned)img->h;
		context->tex.ch = img->true_channels;
		context->tex.bpp = img->bitdepth;
	}
	return success;
}

bool gl_texture_reuse(const struct raw_img *img, struct gl_context *context) {
	const bool reusable = img->w == context->tex.w
		&& img->h == context->tex.h
		&& img->true_channels == context->tex.ch
		&& img->bitdepth == context->tex.bpp
		&& (bool)img->palette == context->tex.paletted;

	if (reusable) {
		struct gl_upload_params params;
		set_upload_parameters(img, &params, context);
		return tex_upload(img, context, &params, true);
	}
	return false;
}

void gl_draw(void) {
	glClear(GL_COLOR_BUFFER_BIT);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

void gl_clear_color(const float bg[static 4]) {
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
/*
static void debug_print(GLenum source, GLenum type, GLuint id, GLenum severity,
GLsizei length, const GLchar *message, const void *user_data) {
	(void)source;
	(void)type;
	(void)id;
	(void)severity;
	(void)user_data;
	fwrite(message, 1, (size_t)length, stdout);
}*/

bool gl_context_setup(struct gl_context *context, struct wu_conf *wuconf) {
	printf("vendor: %s\n"
		"renderer: %s\n"
		"version: %s\n"
		"shading: %s\n",
		glGetString(GL_VENDOR), glGetString(GL_RENDERER),
		glGetString(GL_VERSION), glGetString(GL_SHADING_LANGUAGE_VERSION));


//	glDebugMessageCallback(debug_print, NULL);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	if (!wuconf->bg_src) {
		GLfloat bg[4];
		for (size_t i = 0; i < sizeof(wuconf->bg); ++i) {
			bg[i] = (float)wuconf->bg[i] / (float)UCHAR_MAX;
		}
		glClearColor(bg[0], bg[1], bg[2], bg[3]);
	}

	GLuint mts;
	glGetIntegerv(GL_MAX_TEXTURE_SIZE, (GLint *)&mts);
	if (wuconf->max_img_size) {
		wuconf->max_img_size = umin(wuconf->max_img_size, mts);
	} else {
		wuconf->max_img_size = mts;
	}

	GLuint vertex_array;
	glGenVertexArrays(1, &vertex_array);
	glBindVertexArray(vertex_array);

	GLbyte vertices[] = {
		-1,-1,  1,-1,
		-1, 1,  1, 1,
	};
	GLuint array_buf[2];
	glGenBuffers(ARRAY_LEN(array_buf), array_buf);
	glBindBuffer(GL_ARRAY_BUFFER, array_buf[0]);
	glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices,
		GL_STATIC_DRAW);
	context->pixel_unpack_buf = array_buf[1];

	const char *vs =
		"#version 330 core\n"
		"in vec2 pos;"
		"out vec2 texcoord;"
		"uniform mat4 trans;"
		"void main() {"
			"texcoord = pos * vec2(0.5) + vec2(0.5);"
			"gl_Position = trans * vec4(pos, 0, 1);"
		"}";
	const char *fs =
		"#version 330 core\n"
		"in vec2 texcoord;"
		"out vec4 color;"
		"uniform sampler2D img;"
		"uniform sampler2D pal;"
		"uniform bool use_palette;"
		"uniform bool checker_alpha;"
		"lowp vec3 gen_check_pattern() {"
			"vec2 d = floor(texcoord / (fwidth(texcoord) * 16.0));"
			"const float shade = 1.0 / 3.0;"
			"float checker = mod(d.x + d.y, 2.0) * shade + shade;"
			"return mix(vec3(checker), color.rgb, color.a);"
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

	GLuint texs[2];
	glGenTextures(ARRAY_LEN(texs), texs);
	const GLuint img = texs[0];
	const GLuint pal = texs[1];

	/* Do mind the 'GL_CLAMP_TO_EDGE' here, paletted images won't work
	 * without it. */
	setup_texture(pal, GL_TEXTURE_2D, pal_samp, 1, GL_CLAMP_TO_EDGE,
		GL_NEAREST, GL_NEAREST, 0);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 256, 1, 0, GL_RGBA,
		GL_UNSIGNED_BYTE, NULL);
	swizzle_set(GL_TEXTURE_2D, pix_rgba, true);

	setup_texture(img, GL_TEXTURE_2D, img_samp, 0, GL_CLAMP_TO_BORDER,
		GL_LINEAR_MIPMAP_LINEAR, GL_NEAREST, 6);

	glGenQueries(1, &context->timer);

	context->tex.w = 0;
	context->tex.h = 0;
	context->tex.ch = 0;
	context->tex.bpp = 0;
	context->tex.paletted = false;
	return true;
}

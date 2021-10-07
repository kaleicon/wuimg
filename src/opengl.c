#include <stdio.h>
#include <math.h>
#include <limits.h>
#include <string.h>

#include <epoxy/gl.h>

#include "wudefs.h"
#include "common.h"
#include "opengl.h"
#include "raster/unpack.h"

#define WU_GL_DEBUG

#define ATTR_POS "pos"
#define UNI_IMG "img"
#define UNI_PAL "pal"
#define UNI_MATRIX "matrix"
#define UNI_ENABLE_PAL "enable_pal"
#define UNI_ENABLE_CHECKERS "enable_checkers"

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
	GLint texs[2];
	glGetIntegerv(GL_TEXTURE_BINDING_2D, texs);
	glActiveTexture(GL_TEXTURE1);
	glGetIntegerv(GL_TEXTURE_BINDING_1D, texs + 1);
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
		GLenum tex = GL_TEXTURE_2D;
		if (context->tex.paletted) {
			tex = GL_TEXTURE_1D;
			glActiveTexture(GL_TEXTURE1);
		}
		glTexParameteri(tex, GL_TEXTURE_SWIZZLE_A,
			(alpha == gl_alpha_opaque) ? GL_ONE : context->tex.alpha_swizzle);
		if (context->tex.paletted) {
			glActiveTexture(GL_TEXTURE0);
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

static void bind_buffer_data(const GLenum target, const GLuint buffer,
const size_t size, const GLvoid *data, const GLenum usage) {
	glBindBuffer(target, buffer);
	glBufferData(target, (GLsizeiptr)size, data, usage);
}

static GLint swizzle_set(const GLenum tex_type, const enum pix_layout layout,
const bool alpha) {
	const GLint lut[] = {GL_RED, GL_GREEN, GL_BLUE, GL_ALPHA};
	GLint swizzle[4];
	for (size_t i = 0; i < ARRAY_LEN(swizzle); ++i) {
		const size_t sh = 6 - i*2;
		if (i == 3 && !alpha) {
			swizzle[i] = GL_ONE;
		} else {
			swizzle[i] = lut[(layout >> sh) & 0x3];
		}
	}
	glTexParameteriv(tex_type, GL_TEXTURE_SWIZZLE_RGBA, swizzle);
	return swizzle[3];
}
__attribute__((unused))
static void print_quaternary(const enum pix_layout layout) {
	char digits[4];
	for (size_t i = 0; i < ARRAY_LEN(digits); ++i) {
		const size_t sh = 6 - i*2;
		digits[i] = ((layout >> sh) & 0x3) + '0';
	}
	fwrite(digits, 1, sizeof(digits), stdout);
}

static enum pix_layout layout_equiv(const enum pix_layout layout,
const bool is_bgra, const bool reverse) {
	/* On some cards GL_BGRA seems to be required for good texture upload
	 * performance, moreso for packed formats. We modify the layout to
	 * upload with good parameters and then fix the colors by swizzling. */
	enum pix_layout out = 0;
	for (int i = 0; i < 4; ++i) {
		int sh = 6 - i*2;
		uint8_t col = (layout >> sh) & 0x3;
		if (is_bgra) {
			switch (sh) {
			case 6: sh = 2; break;
			case 2: sh = 6; break;
			}
		}
		if (reverse) {
			out |= col << (6 - sh);
		} else {
			out |= col << sh;
		}
	}
	return out;
}

static void toggle_palette(struct gl_context *context) {
	context->tex.paletted = !context->tex.paletted;
	glUniform1i(context->uni.use_pal, context->tex.paletted);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
		context->tex.paletted ? GL_NEAREST : GL_LINEAR_MIPMAP_LINEAR);
}

static bool set_upload_parameters(struct gl_context *context,
const struct raw_img *img, struct gl_upload_params *params) {
	enum pix_layout layout = img->layout;
	if (img->palette) {
		if (img->bitdepth > 8) {
			return false;
		}

		glActiveTexture(GL_TEXTURE1);
		glTexSubImage1D(GL_TEXTURE_1D, 0, 0, 1 << img->bitdepth,
			GL_RGBA, GL_UNSIGNED_BYTE, img->palette);
		context->tex.alpha_swizzle = swizzle_set(GL_TEXTURE_1D, layout,
			!img->no_alpha);
		glActiveTexture(GL_TEXTURE0);

		if (!context->tex.paletted) {
			toggle_palette(context);
		}

		*params = (struct gl_upload_params) {
			.type = GL_UNSIGNED_BYTE,
			.fmt = GL_RED,
			.in_fmt = GL_R8,
			.layout = pix_gray,
			.convert = img->bitdepth < 8,
		};
		return true;
	} else {
		if (context->tex.paletted) {
			toggle_palette(context);
		}
	}

	switch (img->attr) {
	case pix_packing_332:
		*params = (struct gl_upload_params) {
			.type = GL_UNSIGNED_BYTE_3_3_2,
			.fmt = GL_RGB,
			.in_fmt = GL_R3_G3_B2,
			.convert = false,
		};
		break;
	case pix_packing_1555:
		*params = (struct gl_upload_params) {
			.type = GL_UNSIGNED_SHORT_1_5_5_5_REV,
			.fmt = GL_BGRA,
			.in_fmt = GL_RGB5_A1,
			.convert = false,
		};
		layout = layout_equiv(layout, true, true);
		break;
	case pix_normal:
	case pix_inverted:
	case pix_float:
		switch (img->bitdepth) {
		case 4:
			if (img->channels == 4) {
				*params = (struct gl_upload_params) {
					.type = GL_UNSIGNED_SHORT_4_4_4_4_REV,
					.fmt = GL_BGRA,
					.in_fmt = GL_RGBA4,
					.convert = false,
				};
				layout = layout_equiv(layout, true, true);
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
			const GLenum fmt_lut[] = {GL_RED, GL_RG, GL_BGR, GL_BGRA};
			const GLint in_fmt_lut[] = {GL_RED, GL_RG, GL_RGB, GL_RGBA};

			const int bd = imin((img->bitdepth + 7) / 8 - 1, 3);
			*params = (struct gl_upload_params) {
				.type = type_lut[bd],
				.fmt = fmt_lut[img->channels - 1],
				.in_fmt = in_fmt_lut[img->channels - 1],
				.convert = img->bitdepth < 8 || img->bitdepth == 24
					|| img->bitdepth > 32 || img->attr == pix_inverted,
			};
			if (img->channels >= 3) {
				layout = layout_equiv(layout, true, false);
			}
			break;
		default:
			return false;
		}
		break;
	}
	params->layout = layout;
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
		const size_t size = unpack_stride_len(img->w * img->channels,
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

	const GLenum err = glGetError();
	if (err) {
		printf("Encountered error %x when uploading to texture: %s\n",
			err, gl_strerror(err));
		return gl_upload_fail;
	}

	const GLint alpha = swizzle_set(GL_TEXTURE_2D, params->layout,
		!img->no_alpha && context->alpha != gl_alpha_opaque);

	context->tex.w = (unsigned)img->w;
	context->tex.h = (unsigned)img->h;
	context->tex.ch = img->channels;
	context->tex.bpp = img->bitdepth;
	if (!context->tex.paletted) {
		context->tex.alpha_swizzle = alpha;
	}
	glGenerateMipmap(GL_TEXTURE_2D);
	return reuse ? gl_upload_reused : gl_upload_success;
}

enum gl_upload_status gl_texture_upload(struct gl_context *context,
const struct raw_img *img) {
	struct gl_upload_params params;
	if (!set_upload_parameters(context, img, &params)) {
		printf("Invalid channel/bitdepth combination (%d/%d). Skipping.\n",
			img->channels, img->bitdepth);
		return false;
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
const GLint *samps, const GLenum target, const GLint wrap,
const GLint min_filter, const GLint mag_filter, const GLint max_level) {
	glActiveTexture((GLenum)(GL_TEXTURE0 + idx));
	glBindTexture(target, texs[idx]);
	glUniform1i(samps[idx], idx);

	glTexParameteri(target, GL_TEXTURE_WRAP_S, wrap);
	glTexParameteri(target, GL_TEXTURE_WRAP_T, wrap);
	glTexParameteri(target, GL_TEXTURE_MIN_FILTER, min_filter);
	glTexParameteri(target, GL_TEXTURE_MAG_FILTER, mag_filter);
	glTexParameteri(target, GL_TEXTURE_MAX_LEVEL, max_level);
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
		"uniform sampler1D " UNI_PAL ";"
		"uniform bool " UNI_ENABLE_PAL ";"
		"uniform bool " UNI_ENABLE_CHECKERS ";"
		"vec4 check_pattern(vec4 fg) {"
			"vec2 d = floor(texcoord / (fwidth(texcoord) * 16));"
			"vec3 bg = vec3(mod(d.x + d.y, 2.0) * .25 + .5);"
			"return vec4(mix(bg, fg.rgb, fg.a), 1);"
		"}"
		"void main() {"
			"color = texture2D(" UNI_IMG ", texcoord);"
			"if (" UNI_ENABLE_PAL ") {"
				"color = texture1D(" UNI_PAL ", color.r);"
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
		.use_pal = glGetUniformLocation(program, UNI_ENABLE_PAL),
		.checkers = glGetUniformLocation(program, UNI_ENABLE_CHECKERS),
	};
	const GLint samps[] = {
		glGetUniformLocation(program, UNI_IMG),
		glGetUniformLocation(program, UNI_PAL)
	};
	if (context->uni.matrix == -1 || context->uni.use_pal == -1
	|| context->uni.checkers == -1 || samps[0] == -1 || samps[1] == -1) {
		return false;
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

	/* Do mind the 'GL_CLAMP_TO_EDGE' here, paletted images won't work
	 * without it. */
	setup_texture(1, texs, samps, GL_TEXTURE_1D,
		GL_CLAMP_TO_EDGE, GL_NEAREST, GL_NEAREST, 0);
	glTexImage1D(GL_TEXTURE_1D, 0, GL_RGBA, 256, 0, GL_RGBA,
		GL_UNSIGNED_BYTE, NULL);
	swizzle_set(GL_TEXTURE_1D, pix_rgba, true);

	// Image texture is set last to leave GL_TEXTURE0 active.
	setup_texture(0, texs, samps, GL_TEXTURE_2D,
		GL_CLAMP_TO_BORDER, GL_LINEAR_MIPMAP_LINEAR, GL_NEAREST, 6);


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

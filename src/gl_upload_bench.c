#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "common.h"
#include "display.h"
#include "opengl.h"

struct gl_size_params {
	unsigned size;
	GLenum param;
	const char *name;
};

struct gl_params {
	GLint param;
	const char *name;
};

static GLenum texture_upload(const struct raw_img *img,
const struct gl_size_params *type, const struct gl_size_params *fmt,
const struct gl_params *in_fmt, const GLsizei w, const GLsizei h) {
	glTexImage2D(GL_TEXTURE_2D, 0, in_fmt->param, w, h, 0, fmt->param,
		type->param, img->data);
	glGenerateMipmap(GL_TEXTURE_2D);
	return glGetError();
}

static GLenum bench(const struct raw_img *img, const struct gl_context *gl,
const struct gl_size_params *type, const struct gl_size_params *fmt,
const struct gl_params *in_fmt, const GLint align) {
	glPixelStorei(GL_UNPACK_ALIGNMENT, align);
	printf("%s/%s/%s/%d", type->name, fmt->name, in_fmt->name, align);

	const size_t div = type->size * fmt->size;
	GLsizei width = (GLsizei)(img->w / div);
	GLsizei height = (GLsizei)img->h;

	for (int a = 0; a < 12; ++a) {
		gl_draw();
		gl_clock_start(gl);
		GLenum r = texture_upload(img, type, fmt, in_fmt, width, height);
		printf("\t%lu", gl_clock_end(gl));
		if (r != GL_NO_ERROR) {
			return r;
		}
		fflush(stdout);

//		texture_upload(img, type, fmt, in_fmt, 0, 0);
	}

	putchar('\n');
	return GL_NO_ERROR;
}

static GLenum run_benchs(const struct raw_img *img, const struct gl_context *gl) {
	const struct gl_size_params types[] = {
		{1, GL_UNSIGNED_BYTE, "GL_UNSIGNED_BYTE"},
		{2, GL_UNSIGNED_SHORT, "GL_UNSIGNED_SHORT"},
		{4, GL_UNSIGNED_INT, "GL_UNSIGNED_INT"},
		{2, GL_HALF_FLOAT, "GL_HALF_FLOAT"},
		{4, GL_FLOAT, "GL_FLOAT"},
		{2, GL_UNSIGNED_SHORT_4_4_4_4, "GL_UNSIGNED_SHORT_4_4_4_4"},
		{2, GL_UNSIGNED_SHORT_4_4_4_4_REV, "GL_UNSIGNED_SHORT_4_4_4_4_REV"},
		{4, GL_UNSIGNED_INT_8_8_8_8, "GL_UNSIGNED_INT_8_8_8_8"},
		{4, GL_UNSIGNED_INT_8_8_8_8_REV, "GL_UNSIGNED_INT_8_8_8_8_REV"},
	};

	struct gl_size_params fmts[] = {
		{1, GL_RED, "GL_RED"},
		{2, GL_RG, "GL_RG"},
		{3, GL_RGB, "GL_RGB"},
		{3, GL_BGR, "GL_BGR"},
		{4, GL_RGBA, "GL_RGBA"},
		{4, GL_BGRA, "GL_BGRA"},
	};

	struct gl_size_params type_fmts[] = { // For the _REV types and friends
		{1, GL_RGBA, "GL_RGBA"},
		{1, GL_BGRA, "GL_BGRA"},
	};

	struct gl_params in_fmts[] = {
		{GL_RED, "GL_RED"},
		{GL_R8, "GL_R8"},
		{GL_R16, "GL_R16"},
		{GL_R16F, "GL_R16F"},
		{GL_R32F, "GL_R32F"},

		{GL_RG, "GL_RG"},
		{GL_RG8, "GL_RG8"},
		{GL_RG16, "GL_RG16"},
		{GL_RG16F, "GL_RG16F"},
		{GL_RG32F, "GL_RG32F"},

		{GL_RGB, "GL_RGB"},
		{GL_RGB4, "GL_RGB4"},
		{GL_RGB5, "GL_RGB5"},
		{GL_RGB8, "GL_RGB8"},
		{GL_RGB10, "GL_RGB10"},
		{GL_RGB12, "GL_RGB12"},
		{GL_RGB16, "GL_RGB16"},
		{GL_RGB16F, "GL_RGB16F"},
		{GL_RGB32F, "GL_RGB32F"},

		{GL_RGBA, "GL_RGBA"},
		{GL_RGBA2, "GL_RGBA2"},
		{GL_RGBA4, "GL_RGBA4"},
		{GL_RGB5_A1, "GL_RGB5_A1"},
		{GL_RGBA8, "GL_RGBA8"},
		{GL_RGB10_A2, "GL_RGB10_A2"},
		{GL_RGBA12, "GL_RGBA12"},
		{GL_RGBA16, "GL_RGBA16"},
		{GL_RGBA16F, "GL_RGBA16F"},
		{GL_RGBA32F, "GL_RGBA32F"},
	};

	glPixelStorei(GL_UNPACK_SWAP_BYTES, GL_FALSE);

	const size_t size_of_types = ARRAY_LEN(types);
	const size_t size_of_infmts = ARRAY_LEN(in_fmts);
	const int tries = 8;

	size_t combinations = 0;
	for (size_t i = 0; i < size_of_types; ++i) {
		struct gl_size_params *valid_fmts = fmts;
		size_t size_of_fmts = ARRAY_LEN(fmts);
		if (i >= 5) {
			valid_fmts = type_fmts;
			size_of_fmts = ARRAY_LEN(type_fmts);
		}
		for (size_t j = 0; j < size_of_fmts; ++j) {
			for (size_t k = 0; k < size_of_infmts; ++k) {
				for (GLint a = 0; a < 4; ++a) {
					GLenum r = bench(img, gl, types + i,
						valid_fmts + j, in_fmts + k,
						1 << a);
					if (r != GL_NO_ERROR) {
						return r;
					}
					++combinations;
				}
			}
		}
	}

	printf("%zu combinations tested %d times\n", combinations, tries);
	return GL_NO_ERROR;
}

int main(const int argc, const char *argv[]) {
	if (argc == 1 || !strcmp(argv[1], "-h") || !strcmp(argv[1], "--help")) {
		printf("Usage: %s TEST_BUFFER_WIDTH\n", argv[0]);
		return 0;
	}

	const char *err_msg = NULL;
	size_t dim = 0;
	if (sscanf(argv[1], "%zu", &dim) == 1 && dim) {
		struct window_context window = {0};
		if (display_setup(&window, NULL)) {
			struct image_file *file = &window.pub.image.file;
			struct raw_img *img = alloc_sub_images(file, 1);
			if (img) {
				*img = (struct raw_img) {
					.data = calloc((dim + 7) * 16, dim),
					.w = dim,
					.h = dim,
					.channels = 4,
					.bitdepth = 8,
				};
				if (img->data) {
					const GLenum err = run_benchs(img,
						&window.pub.gl);
					if (err != GL_NO_ERROR) {
						err_msg = gl_strerror(err);
					}

				} else {
					err_msg = "Alloc failure.";
				}
				image_file_free(file);
			} else {
				err_msg = "Failed to allocate image struct.";
			}
			display_end(&window, NULL);
		} else {
			err_msg = "Failed to set GL context.";
		}
	} else {
		err_msg = "WIDTH must be a positive number.";
	}

	if (err_msg) {
		fprintf(stderr, "Test error: %s\n", err_msg);
		return 1;
	}
	return 0;
}

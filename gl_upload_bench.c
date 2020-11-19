#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdbool.h>

#include "common.h"
#include "display.h"
#include "opengl.h"
#include "dec/pnm.h"

struct gl_params {
	GLenum param;
	const char *name;
};

static GLenum texture_upload(const struct raw_img *img, const GLenum type,
const GLenum fmt, const long int in_fmt, bool clear) {
/*	size_t data_div;
	switch (type) {
	case GL_UNSIGNED_SHORT_4_4_4_4:
	case GL_UNSIGNED_SHORT_4_4_4_4_REV:
		data_div = 1;
		break;
	case GL_UNSIGNED_BYTE:
	case GL_UNSIGNED_INT_8_8_8_8:
	case GL_UNSIGNED_INT_8_8_8_8_REV:
		data_div = 2;
		break;
	case GL_UNSIGNED_SHORT:
		data_div = 4;
		break;
	case GL_UNSIGNED_INT:
		data_div = 8;
		break;
	default:
		data_div = 0;
	}*/

	GLsizei width = (GLsizei)img->w;
	GLsizei height = (GLsizei)(img->h/2);// data_div);
	if (clear) {
		width = 3;
		height = 3;
	}

//	printf("(%d x %d, %d %d %ld)", width, height, type, fmt, in_fmt);
	glTexImage2D(GL_TEXTURE_2D, 0, (GLint)in_fmt, width, height, 0, fmt,
		type, img->data);
	const GLenum err = glGetError();
	if (err != GL_NO_ERROR) {
		return err;
	}

	glGenerateMipmap(GL_TEXTURE_2D);
	return GL_NO_ERROR;
}

static GLenum run_benchs(const struct raw_img *img) {
	const struct gl_params types[] = {
		{GL_UNSIGNED_BYTE, "GL_UNSIGNED_BYTE"},
		{GL_UNSIGNED_SHORT, "GL_UNSIGNED_SHORT"},
		{GL_UNSIGNED_INT, "GL_UNSIGNED_INT"},
		{GL_UNSIGNED_SHORT_4_4_4_4, "GL_UNSIGNED_SHORT_4_4_4_4"},
		{GL_UNSIGNED_SHORT_4_4_4_4_REV, "GL_UNSIGNED_SHORT_4_4_4_4_REV"},
		{GL_UNSIGNED_INT_8_8_8_8, "GL_UNSIGNED_INT_8_8_8_8"},
		{GL_UNSIGNED_INT_8_8_8_8_REV, "GL_UNSIGNED_INT_8_8_8_8_REV"},
	};

	struct gl_params fmts[] = {
		{GL_RED, "GL_RED"},
		{GL_RG, "GL_RG"},
		{GL_RGB, "GL_RGB"},
		{GL_BGR, "GL_BGR"},
		{GL_RGBA, "GL_RGBA"},
		{GL_BGRA, "GL_BGRA"},
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
//	const size_t size_of_fmts = ARRAY_LEN(fmts);
	const size_t size_of_infmts = ARRAY_LEN(in_fmts);
	const int retries = 8;

	size_t combinations = 0;
	struct timespec start_total;
	clock_start(&start_total);
	for (size_t i = 0; i < size_of_types; ++i) {
		size_t size_of_fmts = ARRAY_LEN(fmts);
		struct gl_params *valid_fmts = fmts;
//		struct gl_params *valid_in = in_fmts;
		if (i >= 3) {
			size_of_fmts = 2;
			valid_fmts = fmts + 4;
//			valid_in = in_fmts + 19;
		}
		for (size_t j = 0; j < size_of_fmts; ++j) {
			for (size_t k = 0; k < size_of_infmts; ++k) {
				printf("%s/%s/%s", types[i].name,
					valid_fmts[j].name, in_fmts[k].name);
				for (int r = 0; r < retries; ++r) {
					glClear(GL_COLOR_BUFFER_BIT);
					glDrawElements(GL_TRIANGLES, 6,
						GL_UNSIGNED_BYTE, 0);
//					glfwSwapBuffers(window);
					glFinish();

					struct timespec start;
					clock_start(&start);
					GLenum r = texture_upload(img,
						types[i].param,
						valid_fmts[j].param,
						in_fmts[k].param, false);
					if (r != GL_NO_ERROR) {
						return r;
					}
					glFinish();
					printf("\t%ld", clock_nanodiff(&start));
					fflush(stdout);

					texture_upload(img, GL_UNSIGNED_BYTE,
						GL_RED,
						in_fmts[(k+size_of_fmts/2)%size_of_fmts].param,
						true);
					glFinish();
					++combinations;
				}
				putchar('\n');
			}
		}
//		if (combinations > 10) {
//			break;
//		}
	}

	printf("%zu combinations total in %ld nanoseconds.\n", combinations,
		clock_nanodiff(&start_total));
	return GL_NO_ERROR;
}

static void help(const char *prog) {
	printf("Usage: %s FILE.pnm\n", prog);
}

int main(const int argc, const char *argv[]) {
	if (argc == 1 || !strcmp(argv[1], "-h") || !strcmp(argv[1], "--help")) {
		help(argv[0]);
	}

	struct window_control control = {0};
	if (!setup_display(&control, NULL)) {
		return 1;
	}

	struct image_file file = {0};
	file.ifp = fopen(argv[1], "rb");
	if (!file.ifp) {
		puts("Failed to open file.");
		return 1;
	}

	const enum wu_error result = pnm_dec(&file, &control.conf);
	if (result == wu_ok) {
		const GLenum err = run_benchs(file.sub_img);
		if (err) {
			printf("Failed at uploading. %s.\n", gl_error_str(err));
		}
	}

	free_image_file(&file);
	delete_gl_context(&control.context);
	terminate_window();

	if (result != wu_ok) {
		printf("Failed at decoding. %s.\n", wu_error_message(result));
		return result;
	}
	return 0;
}

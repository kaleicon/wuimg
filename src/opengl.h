// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2019 kaleido
#ifndef WU_OPENGL
#define WU_OPENGL

#include <epoxy/gl.h>

#include "wudefs.h"
#include "misc/time.h"

// Texture swizzling is the newest feature we require, so 3.3 is the minimum
#define WU_GL_MAJOR 3
#define WU_GL_MINOR 3

enum gl_upload_status {
	gl_upload_fail = 0,
	gl_upload_success,
	gl_upload_same_size,
};

enum gl_uni {
#define GL_UNI(x) gl_uni_##x,
#include "opengl.def"
#undef GL_UNI

	gl_uni_total,
};

struct gl_context {
	GLint uni[gl_uni_total];
	GLuint pixel_unpack_buf;
	GLuint timer;
	GLuint framebuffer;

	struct gl_image_info {
		enum image_mode mode:8;
		enum alpha_interpretation alpha:2;
		unsigned subsamp:4;
		bool no_transform:1;
		bool mirror:1;
		uint8_t rotate;
		uint8_t bitdepth;
		int shown_frame;
		float fit_zoom;
		float ratio;
		float w, h;
	} tex;
	float pix_size[2];

	uint8_t bg[4];
	uint8_t user_alpha;
	bool update;
	bool unmultiply;

	const struct wuimg *img;
	struct color_space tgt;
	cmsHPROFILE icc_tgt;
};

const char * gl_strerror(GLenum error);

void gl_terminate(struct gl_context *context);

watch_t gl_clock_query(const struct gl_context *context);

void gl_alpha_toggle(struct gl_context *context, int cycle);

void gl_viewport(struct gl_context *context, const struct display_dims *dims);

bool gl_draw(struct gl_context *context, const struct wu_state *state);

void gl_clear_color(struct gl_context *context, const uint8_t bg[static 4]);

enum gl_upload_status gl_texture_upload(struct gl_context *context,
const struct wuimg *img, enum heed_ratio heed);

bool gl_subtexture_upload(struct gl_context *context, const struct wuimg *img,
const struct wu_state *state);

const char * gl_context_setup(struct gl_context *context, struct wu_conf *conf);


struct gl_reader_context {
	struct gl_context context;
	struct wu_state state;
	const struct wuimg *dst;
};

void gl_reader_read_row(struct gl_reader_context *reader, size_t y,
void *restrict tgt);

const char * gl_reader_set(struct gl_reader_context *reader,
const struct wuimg *dst, const struct wuimg *src);

bool gl_reader_init(struct gl_reader_context *reader, struct wu_conf *wuconf);

#endif // WU_OPENGL

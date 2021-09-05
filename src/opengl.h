#ifndef OPENGL
#define OPENGL

#include <epoxy/gl.h>

#include "wudefs.h"

// Texture swizzling is the newest feature we require, so 3.3 is the minimum
#define WU_GL_MAJOR 3
#define WU_GL_MINOR 3

struct gl_context {
	struct {
		GLint trans;
		GLint use_pal;
		GLint checkers;
	} uni;
	GLuint pixel_unpack_buf;
	GLuint timer;

	unsigned fb_wh[2];
	struct {
		unsigned w, h;
		unsigned char ch, bpp;
		bool paletted;
	} tex;
};

void gl_context_delete(struct gl_context *context);

GLuint64 gl_clock_end(struct gl_context *context);

void gl_clock_start(struct gl_context *context);

void gl_alpha_state(struct gl_context *context, enum alpha_state alpha);

void gl_matrix_update(const struct gl_context *context, struct wu_state *state);

float gl_fit_zoom(const struct gl_context *context,
unsigned char rotation);

void gl_even_view(struct gl_context *context);

bool gl_texture_upload(const struct raw_img *img, struct gl_context *context);

bool gl_texture_reuse(const struct raw_img *img, struct gl_context *context);

void gl_draw(void);

void gl_clear_color(const float bg[static 4]);

bool gl_context_setup(struct gl_context *context, struct wu_conf *wuconf);

#endif /* OPENGL */

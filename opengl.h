#ifndef OPENGL
#define OPENGL

#include <epoxy/egl.h>

#include "wudefs.h"

struct gl_context {
	struct {
		GLint trans;
		GLint use_pal;
		GLint checkers;
	} uni;
	GLuint pixel_unpack_buf;

	unsigned fb_wh[2];
	struct {
		unsigned w, h;
		unsigned char ch, bpp;
		bool paletted;
	} tex;
};

const char * gl_error_str(GLenum error);

void delete_gl_context(void);

void set_gl_alpha(struct gl_context *context, enum alpha_state alpha);

void update_gl_matrix(const struct gl_context *context, struct wu_state *state);

float calc_gl_fit_zoom(const struct gl_context *context,
unsigned char rotation);

void even_gl_view(struct gl_context *context);

bool load_gl_texture(const struct raw_img *img, struct gl_context *context);

bool reuse_gl_texture(const struct raw_img *img, struct gl_context *context);

void clear_gl_color(const float bg[4]);

bool setup_opengl(struct gl_context *context, struct wu_conf *wuconf);

#endif /* OPENGL */

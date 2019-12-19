#ifndef OPENGL
#define OPENGL

#include <epoxy/gl.h>

#include "wudefs.h"

#define MAX_ZOOM 64
#define MIN_ZOOM (float)(1.0 / MAX_ZOOM)
#define MV_FACTOR (float)(1.0 / 8)

struct gl_context {

	GLuint vertex_array;
	GLuint vertex_buf;
	GLuint element_buf;
	GLuint vertex_shader;
	GLuint fragment_shader;
	GLuint program;
	GLuint texture;

	unsigned int max_tex_size;
	unsigned int tex_w, tex_h, tex_ch, tex_bpp;

	GLfloat zoom;
	GLfloat fit_zoom;
	GLfloat normal_x, normal_y;
	GLfloat offset_x, offset_y;
	GLint scale_uni;
	GLint offset_uni;
};

const char * gl_error_str(GLenum error);

void delete_gl_context(const struct gl_context *context);

void update_gl_scaling(const struct gl_context *context);

void normalize_gl_viewport(struct gl_context *context, int fb_w, int fb_h);

void reset_gl_draw_state(struct gl_context *context);

bool update_gl_texture(const struct raw_img *img, struct gl_context *context);

bool load_gl_texture(const struct raw_img *img, struct gl_context *context);

bool setup_opengl(struct gl_context *context);

#endif /* OPENGL */

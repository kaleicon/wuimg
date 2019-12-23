#ifndef OPENGL
#define OPENGL

#include <epoxy/gl.h>

#include "wudefs.h"

#define MAX_ZOOM 64
#define MIN_ZOOM (float)(1.0 / MAX_ZOOM)
#define MV_FACTOR (float)(1.0 / 8)

struct gl_matrix { // Column-major order
	GLfloat mat11;
	GLfloat mat12;
	GLfloat mat13;
	GLfloat mat14;

	GLfloat mat21;
	GLfloat mat22;
	GLfloat mat23;
	GLfloat mat24;

	GLfloat mat31;
	GLfloat mat32;
	GLfloat mat33;
	GLfloat mat34;

	GLfloat offset_x;
	GLfloat offset_y;
	GLfloat mat43;
	GLfloat scale;
};

struct gl_context {
	GLuint vertex_array;
	GLuint vertex_buf;
	GLuint element_buf;
	GLuint vertex_shader;
	GLuint fragment_shader;
	GLuint program;
	GLuint texture;
	GLint trans_uni;

	unsigned int max_tex_size;
	unsigned int tex_w, tex_h;
	unsigned char tex_ch, tex_bpp;

	unsigned char mirror;
	unsigned char rotate;
	GLfloat fit_zoom;
	union {
		struct gl_matrix trans;
		GLfloat mat[16];
	};
};

const char * gl_error_str(GLenum error);

void delete_gl_context(const struct gl_context *context);

void update_gl_matrix(const struct gl_context *context);

void calc_gl_mirrot(struct gl_context *context);

void change_gl_rotation(struct gl_context *context, const int turns);

void reset_gl_offset(struct gl_context *context);

void change_gl_offset(struct gl_context *context, const float x, const float y);

void set_gl_scaling(struct gl_context *context, const float scale);

void correct_gl_view(struct gl_context *context, const int fb_w,
const int fb_h);

void reset_gl_matrix(struct gl_context *context);

void update_gl_texture(const struct raw_img *img);

bool load_gl_texture(const struct raw_img *img, struct gl_context *context);

bool is_texture_reusable(const struct raw_img *img,
const struct gl_context *context);

bool setup_opengl(struct gl_context *context);

#endif /* OPENGL */

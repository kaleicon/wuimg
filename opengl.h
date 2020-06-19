#ifndef OPENGL
#define OPENGL

#include <epoxy/gl.h>

#include "wudefs.h"

#define MAX_ZOOM 64
#define MIN_ZOOM (float)(1.0 / MAX_ZOOM / 2)
#define MV_FACTOR (float)(1.0 / 4)

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
	GLuint vertex;
	GLuint vertex_shader;
	GLuint fragment_shader;
	GLuint program;
	GLint trans_uni;
	GLint use_pal_uni;
	union {
		struct {GLuint img, pal;} texture;
		GLuint textures[2];
	};


	union {
		struct {int fb_w, fb_h;};
		int fb_wh[2];
	};
	unsigned short tex_w, tex_h;
	unsigned char tex_ch, tex_bpp;

	bool use_palette;
	union {
		struct gl_matrix trans;
		GLfloat mat[16];
	};
};

const char * gl_error_str(GLenum error);

void delete_gl_context(const struct gl_context *context);

void update_gl_matrix(const struct gl_context *context);

float correct_gl_aspect_ratio(struct gl_context *context,
const unsigned char rotation);

void even_gl_view(struct gl_context *context);

void calc_gl_mirrot(struct gl_context *context, const struct wu_state *state);

void change_gl_rotation(struct gl_context *context, struct wu_state *state,
const int turns);

void change_gl_offset(struct gl_context *context, const bool relative,
const float x, const float y);

void set_gl_scaling(struct gl_context *context, struct wu_state *state,
const float new_zoom);

bool load_gl_texture(const struct raw_img *img, struct gl_context *context);

bool reuse_gl_texture(const struct raw_img *img, struct gl_context *context);

void clear_gl_color(const unsigned char fallback_bg[4],
const unsigned char img_bg[4], const enum background_source source);

bool setup_opengl(struct gl_context *context, struct wu_conf *wuconf);

#endif /* OPENGL */

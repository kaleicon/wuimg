#ifndef WU_OPENGL
#define WU_OPENGL

#include <epoxy/gl.h>

#include "wudefs.h"

// Texture swizzling is the newest feature we require, so 3.3 is the minimum
#define WU_GL_MAJOR 3
#define WU_GL_MINOR 3

enum gl_upload_status {
	gl_upload_fail = 0,
	gl_upload_success,
	gl_upload_same_size,
};

struct gl_reader {
	struct display_dims prev;
	size_t len;
	size_t w, h;
	GLenum fmt;
	GLenum type;
	uint8_t ch;
	uint8_t bd;
};

struct gl_context {
	struct gl_uni {
		GLint pos_matrix;
		GLint color_mode;
		GLint alpha_op;
		GLint cms_mode;
		GLint plane_offsets;
		GLint to_rgba;
		GLint cms_mat;
		GLint transfer;
		GLint args;
	} uni;
	GLuint pixel_unpack_buf;
	GLuint timer;
	GLuint framebuffer;

	float fb_wh[2];

	enum image_mode mode:8;
	bool update_matrix;
	uint8_t alpha;
	uint8_t user_alpha;

	struct gl_image_info {
		uint8_t rotate;
		uint8_t mirror;
		float w, h;
	} tex;
	cmsHPROFILE icc;
};

const char * gl_strerror(GLenum error);

void gl_terminate(struct gl_context *context);

GLuint64 gl_clock_query(const struct gl_context *context);

void gl_alpha_toggle(struct gl_context *context, int cycle);

void gl_matrix_update(struct gl_context *context, struct wu_state *state);

float gl_fit_zoom(const struct gl_context *context, uint8_t rotation);

void gl_viewport(struct gl_context *context, const struct display_dims *dims);

enum gl_upload_status gl_texture_upload(struct gl_context *context,
struct raw_img *img);

void gl_draw(const struct gl_context *context);

void gl_clear_color(const uint8_t bg[static 4]);

void gl_reader_read_row(struct gl_context *context, struct wu_state *state,
const struct gl_reader *reader, void *restrict dst, size_t row);

bool gl_reader_set(struct gl_context *context, struct wu_state *state,
struct gl_reader *r, const struct raw_img *img);

void gl_reader_unbind(void);

void gl_reader_bind(struct gl_context *context);

bool gl_context_setup(struct gl_context *context, struct wu_conf *wuconf);

#endif /* WU_OPENGL */

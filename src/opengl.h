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
	gl_upload_reused,
};

enum gl_alpha_mode {
	gl_alpha_enabled = 0,
	gl_alpha_checkers,
	gl_alpha_opaque,
	gl_alpha_STATES,
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
		GLint alpha_mode;
		GLint plane_offsets;
		GLint colorspace;
	} uni;
	GLuint pixel_unpack_buf;
	GLuint timer;
	GLuint framebuffer;

	float fb_wh[2];
	struct gl_texture {
		size_t w, h;
		int hash;
		enum image_mode mode;
	} tex;
	bool update_matrix;
	bool disable_alpha;
	enum gl_alpha_mode alpha:8;
};

const char * gl_strerror(GLenum error);

GLuint64 gl_clock_end(const struct gl_context *context);

void gl_clock_start(const struct gl_context *context);

void gl_alpha_toggle(struct gl_context *context);

void gl_matrix_update(struct gl_context *context, struct wu_state *state);

float gl_fit_zoom(const struct gl_context *context,
unsigned char rotation);

void gl_viewport(struct gl_context *context, const struct display_dims *dims);

enum gl_upload_status gl_texture_upload(struct gl_context *context,
const struct raw_img *img);

void gl_draw(void);

void gl_clear_color(const float bg[static 4]);

void gl_reader_read_row(struct gl_context *context, struct wu_state *state,
const struct gl_reader *reader, void *restrict data, size_t row);

bool gl_reader_set(struct gl_context *context,
struct wu_state *state, struct gl_reader *reader, const struct raw_img *img);

void gl_reader_disable(struct gl_context *context,
const struct gl_reader *reader);

void gl_reader_enable(struct gl_context *context, struct gl_reader *reader);

bool gl_context_setup(struct gl_context *context, struct wu_conf *wuconf);

#endif /* WU_OPENGL */

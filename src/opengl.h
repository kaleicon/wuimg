#ifndef WU_OPENGL
#define WU_OPENGL

#include <epoxy/gl.h>

#include "wudefs.h"

// Texture swizzling is the newest feature we require, so 3.3 is the minimum
#define WU_GL_MAJOR 3
#define WU_GL_MINOR 3

enum gl_update {
	gl_update_none = 0,
	gl_update_redraw,
	gl_update_matrix,
};

enum gl_upload_status {
	gl_upload_fail = 0,
	gl_upload_success,
	gl_upload_same_size,
};

struct gl_reader {
	size_t len;
	size_t w, h;
	GLenum fmt;
	GLenum type;
	uint8_t ch;
	uint8_t bd;
};

struct gl_context {
	struct gl_uni {
		struct gl_uni_mat {
			GLint pos;
			GLint nonlinear;
			GLint cms;
		} mat;
		struct gl_uni_mode {
			GLint color;
			GLint alpha;
			GLint cms;
		} mode;
		struct gl_uni_eotf {
			GLint fn;
			GLint args;
		} eotf;
	} uni;
	GLuint pixel_unpack_buf;
	GLuint timer;
	GLuint framebuffer;

	float pix_size[2];

	enum gl_update update:8;
	uint8_t user_alpha;
	bool unmultiply;

	struct gl_image_info {
		enum alpha_interpretation alpha:8;
		enum image_mode mode:8;
		unsigned subsamp:4;
		bool mirror:1;
		uint8_t rotate;
		float fit_zoom;
		float ratio;
		float w, h;
	} tex;
	cmsHPROFILE icc;
};

const char * gl_strerror(GLenum error);

void gl_terminate(struct gl_context *context);

GLuint64 gl_clock_query(const struct gl_context *context);

void gl_alpha_toggle(struct gl_context *context, int cycle);

void gl_viewport(struct gl_context *context, const struct display_dims *dims);

bool gl_draw(struct gl_context *context, const struct wu_state *state);

void gl_clear_color(const uint8_t bg[static 4]);

enum gl_upload_status gl_texture_upload(struct gl_context *context,
struct wuimg *img);

void gl_reader_read_row(struct gl_context *context, struct wu_state *state,
const struct gl_reader *reader, void *restrict dst, size_t row);

bool gl_reader_set(struct gl_context *context, struct gl_reader *r,
struct wu_state *state, const struct wuimg *img);

void gl_reader_unbind(void);

void gl_reader_bind(struct gl_context *context);

bool gl_context_setup(struct gl_context *context, struct wu_conf *wuconf);

#endif /* WU_OPENGL */

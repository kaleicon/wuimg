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

enum gl_color_mode {
	gl_color_raw,
	gl_color_palette,
	gl_color_yuva,
};

enum gl_alpha {
	gl_alpha_enabled = 0,
	gl_alpha_opaque,
	gl_alpha_checkers,
	gl_alpha_disable,
	gl_alpha_STATES,
};

struct gl_context {
	struct gl_uni {
		GLint matrix;
		GLint color_mode;
		GLint checkers;
	} uni;
	GLuint pixel_unpack_buf;
	GLuint timer;

	float fb_wh[2];
	struct gl_texture {
		unsigned w, h;
		unsigned char ch, bpp;
		enum gl_color_mode mode:8;
		GLint alpha_swizzle;
	} tex;
	enum gl_alpha alpha:8;
	bool update_matrix;
};

void gl_context_delete(struct gl_context *context);

GLuint64 gl_clock_end(struct gl_context *context);

void gl_clock_start(struct gl_context *context);

void gl_alpha_toggle(struct gl_context *context);

void gl_matrix_update(const struct gl_context *context, struct wu_state *state);

float gl_fit_zoom(const struct gl_context *context,
unsigned char rotation);

void gl_viewport(struct gl_context *context, int w, int h);

enum gl_upload_status gl_texture_upload(struct gl_context *context,
const struct raw_img *img);

void gl_draw(void);

void gl_clear_color(const float bg[static 4]);

bool gl_context_setup(struct gl_context *context, struct wu_conf *wuconf);

#endif /* WU_OPENGL */

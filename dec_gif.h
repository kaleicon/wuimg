#ifndef DEC_GIF
#define DEC_GIF

#include "wudefs.h"

enum wu_error gif_callback(struct image_file *infile,
const struct wu_conf *wuconf, struct wu_state *state, enum image_event event);

enum wu_error gif_dec(struct image_file *infile,
const struct wu_conf *wuconf);

#endif /* DEC_GIF */

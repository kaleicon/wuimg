#ifndef DEC_SVG
#define DEC_SVG

#include "../wudefs.h"

enum wu_error svg_callback(struct image_file *infile,
const struct wu_conf *wuconf, struct wu_state *state, enum image_event event);

enum wu_error svg_dec(struct image_file *infile,
const struct wu_conf *wuconf);

#endif // DEC_SVG

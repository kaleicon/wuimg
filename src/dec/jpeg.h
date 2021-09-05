#ifndef DEC_JPEG
#define DEC_JPEG

#include "../wudefs.h"

enum wu_error jpeg_callback(struct image_file *infile,
const struct wu_conf *wuconf, struct wu_state *state, enum image_event ev);

enum wu_error jpeg_dec(struct image_file *infile,
const struct wu_conf *wuconf);

#endif /* DEC_JPEG */

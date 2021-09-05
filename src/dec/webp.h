#ifndef DEC_WEBP
#define DEC_WEBP

#include <stdio.h>

#include "../wudefs.h"

enum wu_error webp_callback(struct image_file *infile,
const struct wu_conf *wuconf, struct wu_state *state, enum image_event event);

enum wu_error webp_dec(struct image_file *infile,
const struct wu_conf *wuconf);

#endif /* DEC_WEBP */

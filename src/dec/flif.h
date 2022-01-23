#ifndef DEC_FLIF
#define DEC_FLIF

#include "../wudefs.h"

enum wu_error flif_callback(struct image_file *infile,
const struct wu_conf *wuconf, struct wu_state *state, enum image_event ev);

enum wu_error flif_dec(struct image_file *infile,
const struct wu_conf *wuconf);

#endif /* DEC_FLIF */

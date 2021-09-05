#ifndef DEC_RAW
#define DEC_RAW

#include "../wudefs.h"

enum wu_error raw_callback(struct image_file *infile,
const struct wu_conf *wuconf, struct wu_state *state, enum image_event ev);

enum wu_error raw_dec(struct image_file *infile, const struct wu_conf *wuconf);

#endif /* DEC_RAW */

#ifndef DEC_G00
#define DEC_G00

#include "../wudefs.h"

enum wu_error g00_callback(struct image_file *infile,
const struct wu_conf *wuconf, struct wu_state *state, enum image_event ev);

enum wu_error g00_dec(struct image_file *infile, const struct wu_conf *wuconf);

#endif /* DEC_G00 */

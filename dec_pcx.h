#ifndef DEC_PCX
#define DEC_PCX

#include "wudefs.h"

enum wu_error pcx_dec(struct image_file *infile, const struct wu_conf *wuconf);

enum wu_error dcx_dec(struct image_file *infile, const struct wu_conf *wuconf);

#endif /* DEC_PCX */

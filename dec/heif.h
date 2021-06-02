#ifndef DEC_HEIF
#define DEC_HEIF

#include "../wudefs.h"

enum wu_error heif_dec(struct image_file *infile,
const struct wu_conf *wuconf);

enum wu_error avif_dec(struct image_file *infile,
const struct wu_conf *wuconf);

#endif /* DEC_HEIF */

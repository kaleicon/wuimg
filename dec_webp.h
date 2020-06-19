#ifndef DEC_WEBP
#define DEC_WEBP

#include <stdio.h>

#include "wudefs.h"

enum wu_error webp_dec(struct image_file *infile,
const struct wu_conf *wuconf);

bool webp_verify(FILE *ifp);

#endif /* DEC_WEBP */

#ifndef DEC_JPEG2000
#define DEC_JPEG2000

#include "wudefs.h"

enum wu_error jp2_dec(struct image_file *infile, const struct wu_conf *wuconf);

enum wu_error j2k_dec(struct image_file *infile, const struct wu_conf *wuconf);

#endif /* DEC_JPEG2000 */

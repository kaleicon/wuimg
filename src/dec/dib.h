#ifndef DEC_DIB
#define DEC_DIB

#include "../wudefs.h"

enum wu_error bmp_dec(struct image_file *infile, const struct wu_conf *wuconf);

enum wu_error dib_dec(struct image_file *infile, const struct wu_conf *wuconf);

enum wu_error ico_dec(struct image_file *infile, const struct wu_conf *wuconf);

#endif /* DEC_DIB */

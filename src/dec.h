#ifndef DEC
#define DEC

#include "wudefs.h"

enum wu_error callback_image(struct image_context *image,
enum image_event event);

enum wu_error decode_image(struct image_context *image);

void print_known_formats(void);

#endif /* DEC */

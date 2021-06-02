#ifndef DEC
#define DEC

#include "wudefs.h"

enum wu_error callback_image(struct image_context *image,
enum image_event event);

enum wu_error decode_image(struct image_context *image);

//char ** filter_directory(const char *restrict dirname,
//const char *restrict init_name, size_t *nr);

void print_known_formats(void);

#endif /* DEC */

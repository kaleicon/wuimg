#ifndef DEC
#define DEC

#include "wudefs.h"

enum wu_error dec_callback_image(struct image_context *image,
enum image_event event);

void dec_free_image(struct image_context *image);

enum wu_error dec_decode_image(struct image_context *image);

enum wu_error dec_iter_image(struct image_context *image,
const struct raw_img **img);

void print_known_formats(void);

#endif /* DEC */

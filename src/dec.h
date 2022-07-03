#ifndef DEC
#define DEC

#include <stdbool.h>

#include "wudefs.h"
#include "wustr.h"

bool fmtmap_known_extension(const struct wuptr filename);

enum wu_error dec_callback_image(struct image_context *image,
enum image_event event);

void dec_free_image(struct image_context *image);

enum wu_error dec_decode_image(struct image_context *image);

enum wu_error dec_iter_image(struct image_context *image,
struct raw_img **cur_img);

void print_known_formats(void);

#endif /* DEC */

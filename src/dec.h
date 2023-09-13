// SPDX-License-Identifier: 0BSD
#ifndef DEC
#define DEC

#include <stdbool.h>

#include "wudefs.h"
#include "misc/wustr.h"

bool fmtmap_known_extension(const struct wuptr filename);

enum wu_error dec_callback_manual(struct image_file *infile,
const struct wu_conf *wuconf, struct wu_state *state,
enum image_event ev, const struct image_fn *fn);

enum wu_error dec_decode_manual(struct image_file *infile,
const struct wu_conf *wuconf, struct wu_state *state,
const struct image_fn *fn);

enum wu_error dec_callback(struct image_context *image,
enum image_event event);

void dec_free_image(struct image_context *image);

enum wu_error dec_decode(struct image_context *image);

enum wu_error dec_iter(struct image_context *image,
struct wuimg **cur_img);

void print_known_formats(void);

#endif /* DEC */

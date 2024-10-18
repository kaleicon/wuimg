// SPDX-License-Identifier: 0BSD
#ifndef DEC
#define DEC

#include <stdbool.h>

#include "wudefs.h"
#include "misc/wustr.h"

bool fmtmap_known_extension(const struct wuptr filename);


void dec_free_image(struct image_context *image);

enum wu_error dec_callback(struct image_context *image,
enum image_event event);

enum wu_error dec_decode(struct image_context *image);

enum wu_error dec_iter(struct image_context *image,
struct wuimg **cur_img);


void dec_src_mem(struct image_context *image, struct wuptr data,
const char *name, const struct image_fn *fn);

void dec_src_file(struct image_context *image, FILE *ifp, const char *name,
bool keep_file, bool stat_file);

void dec_src_filename(struct image_context *image, const char *filename);


void print_known_formats(FILE *ofp);

#endif /* DEC */

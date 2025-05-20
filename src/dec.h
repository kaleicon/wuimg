// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2019 kaleido
#ifndef DEC
#define DEC

#include "wudefs.h"
#include "misc/wustr.h"

void dec_free(struct image_context *image);

enum wu_error dec_callback(struct image_context *image,
enum image_event event);

enum wu_error dec_decode(struct image_context *image);

enum wu_error dec_iter(struct image_context *image,
struct wuimg **cur_img);


void dec_src_auto_desc(struct image_context *image, const struct wuptr *desc);

void dec_src_mem(struct image_context *image, struct wuptr data,
const char *name, const struct image_fn *fn);

void dec_src_file(struct image_context *image, FILE *ifp, const char *name,
bool keep_file, bool stat_file);

void dec_src_filename(struct image_context *image, const char *filename);

#endif /* DEC */

// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2019 kaleido
#ifndef DEC
#define DEC

#include "wudefs.h"
#include "dec_fmt_desc.h"

struct image_context {
	const char *name;
	struct image_file file;
	struct wu_state state;
	struct wu_conf conf;
	struct fmt_desc desc;
};

enum image_event image_cur_events(const struct image_context *image);

struct wuimg * image_cur_sub_img(const struct image_context *image);

bool image_cur_is_anim(const struct image_context *image);

enum image_event image_zoom(struct image_context *image, float new_zoom);

enum image_event image_sub_cycle(struct image_context *image, int steps);

enum image_event image_frame_cycle(struct image_context *image, int steps);

void image_reset(struct image_context *image);


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

// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2019 kaleido
#ifndef DEC
#define DEC

#include "wudefs.h"
#include "dec_fmt_desc.h"

struct wudec_image {
	const char *name;
	struct image_file file;
	struct wu_state state;
	struct wu_conf conf;
	struct fmt_desc desc;
};

enum image_event wudec_cur_events(const struct wudec_image *image);

struct wuimg * wudec_cur_sub_img(const struct wudec_image *image);

bool wudec_cur_is_anim(const struct wudec_image *image);

enum image_event wudec_zoom(struct wudec_image *image, float new_zoom);

enum image_event wudec_sub_cycle(struct wudec_image *image, int steps);

enum image_event wudec_frame_cycle(struct wudec_image *image, int steps);


void wudec_free(struct wudec_image *image);

void wudec_recycle(struct wudec_image *image);

enum wu_error wudec_callback(struct wudec_image *image,
enum image_event event);

enum wu_error wudec_decode(struct wudec_image *image);

enum wu_error wudec_iter(struct wudec_image *image,
struct wuimg **cur_img);


void wudec_src_auto_desc(struct wudec_image *image, const struct wuptr *desc);

void wudec_src_mem(struct wudec_image *image, struct wuptr data,
const char *name, const struct image_fn *fn);

void wudec_src_file(struct wudec_image *image, FILE *ifp, const char *name,
bool keep_file, bool stat_file);

void wudec_src_filename(struct wudec_image *image, const char *filename);

#endif /* DEC */

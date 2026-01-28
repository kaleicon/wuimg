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

// Free and reset struct, keeping conf and transformation state (zoom, offset)
void wudec_recycle_state(struct wudec_image *image);

// Free and reset struct, keeping conf
void wudec_recycle_conf(struct wudec_image *image);

enum wu_error wudec_callback(struct wudec_image *image,
enum image_event event);

// Opens and decodes the first sub-image
enum wu_error wudec_decode(struct wudec_image *image);

/* Decodes `src` and moves first sub-image to `img` and error log to `infile`.
 * `src` must be freed afterwards. */
struct wu_st wudec_decode_embedded(struct image_file *infile, struct wuimg *img,
struct wudec_image *src);

enum wu_error wudec_iter(struct wudec_image *image,
struct wuimg **cur_img);


// Use `fmt` for decoding instead of identifying the file format
void wudec_src_format(struct wudec_image *image, const struct fmt_desc *fmt);

// Use `fn` for decoding
void wudec_src_dec_fn(struct wudec_image *image, const struct image_fn *fn);

// Use `desc` as spec string for the auto decoder
void wudec_src_auto_desc(struct wudec_image *image, const struct wuptr *desc);

// Read input data from memory
void wudec_src_mem(struct wudec_image *image, struct wuptr data,
const char *name);

/* Read input data from an open FILE, which must be seekable and may have an
 * initial offset. */
void wudec_src_file(struct wudec_image *image, FILE *ifp, const char *name,
bool keep_file, bool stat_file);

// Read input data by opening `filename`
void wudec_src_filename(struct wudec_image *image, const char *filename);

#endif /* DEC */

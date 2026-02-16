// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2022 kaleido
#ifndef WUIMG
#define WUIMG

#include <stddef.h>
#include <stdbool.h>

#include "conf.h"
#include "misc/endian.h"
#include "misc/wutree.h"
#include "raster/alpha.h"
#include "raster/bitfield.h"
#include "raster/color.h"
#include "raster/compost.h"
#include "raster/err.h"
#include "raster/pal.h"
#include "raster/pix.h"
#include "raster/strip.h"

struct plane_dim {
	/* Subsampling factor. A ceil division of the image dimensions and
	 * this returns the plane dimensions. */
	uint8_t subsamp;

	/* Sample cositing. Briefly explained, this is the issue of who is
	 * chroma sitting with.
	 *   Consider a luma row and a horizontally subsampled chroma row:
	 *	Luma                Chroma
	 *	+---+---+---+---+   +---+---+---+---+
	 *	| 0 | 1 | 2 | 3 |   |   0   |   1   |
	 *	+---+---+---+---+   +-------+-------+
	 *   When chroma is cosited, it means the center of a chroma pixel
	 * matches the position of a luma pixel (they are sitting together).
	 *   For instance, sampling luma[0] requires sampling chroma[0],
	 * luma[1] requires chroma[0.5] (that is, interpolating equally between
	 * chroma[0] and chroma[1]), luma[2] requires chroma[1], and so on.
	 *   This seems like the natural way to interpret chroma, but implies
	 * that the chroma plane is offset by -0.5 pixels relative to luma.
	 * If we draw their outlines, they don't overlap perfectly.
	 *
	 *   The other mode is midpoint (or center) positioning. Chroma pixels
	 * sit midway between luma pixels. So chroma[0] would be where
	 * luma[0.5] would be, chroma[1] with luma[2.5], etc. Seen the other
	 * way, luma[0] samples chroma[-0.25] (the result of (0 - 0.5)/2, which
	 * would get clamped to 0), luma[1] samples chroma[0.25], and luma[2]
	 * samples chroma[0.75].
	 *   With midpoint positioning, both plane outlines overlap perfectly,
	 * and so it would seem like the natural way to interpret chroma.
	 *   Cositing is only meaningful for subsampling factors == 2 (like
	 * YUV422 and YUV420).
	*/
	bool cosit;
};

struct plane_info {
	unsigned char *ptr;
	struct plane_dim x, y;
	size_t w, h;
	size_t stride;
	size_t size;
};

struct image_planes {
	align_t v_pad;
	struct plane_info p[];
};

enum image_mode {
	image_mode_raw = 0,
	image_mode_palette,
	image_mode_planar,
	image_mode_bitfield,
};

struct frame_info {
	struct compost reg;
	struct frame_time {
		uint32_t num, den;
	} sec;
	bool keyframe;
};

struct image_frames {
	size_t nr;
	int current; // Frame currently rendered in .data
	struct frame_info f[];
};

struct wuimg {
	unsigned char *restrict data;

	size_t w, h;
	unsigned char channels;
	unsigned char bitdepth; // Bits per channel
	align_t align_sh; /* Row memory alignment. If this equals
		`align_bitpack`, rows may start in the middle of a byte. */

	unsigned char bitrange; /* Number of bits used within each channel,
		such that `(1 << bitrange) - 1` corresponds to max brightness.
		Must be <= bitdepth.
		For paletted images, this applies to the palette itself. */
	enum pix_layout layout:8;
	enum pix_attr attr:7;
	enum endianness bit:1; // Bit reading order for depths < 8

	unsigned char rotate; // Clockwise quarter turns
	bool mirror:1; // Vertical mirror. Horizontal is mirror + 2rotate
	enum alpha_interpretation alpha:2;

	bool borrowed:1; // .data is not ours
	bool evolving:1; // Data changes with time
	bool scalable:1; // The decoder will draw according to the window size
	enum image_mode mode:2;
	union {
		struct palette *palette;
		struct image_planes *planes;
		struct bitfield *bitfield;
	} u;

	/* Pixel ratio, the result of horizontal_size/vertical_size.
	 * For a square that is N pixels tall, its width must be N/ratio
	 * pixels for it to look square. */
	float ratio;

	struct color_space cs;
	struct image_frames *frames;

	struct wutree *metadata;
};

struct wutree * wuimg_get_metadata(struct wuimg *img);

void wuimg_aspect_ratio(struct wuimg *img, unsigned h_size, unsigned v_size);

void wuimg_exif_orientation(struct wuimg *img, int orientation);

enum wu_error wuimg_verify(struct wuimg *img);

struct wu_st wuimg_verify_st(struct wuimg *img);

bool wuimg_exceeds_limit(const struct wuimg *img, const struct wu_conf *wuconf);

size_t wuimg_stride(const struct wuimg *img);

size_t wuimg_size(const struct wuimg *img);

bool wuimg_alloc_noverify(struct wuimg *img);

enum wu_error wuimg_alloc(struct wuimg *img);

enum wu_error wuimg_alloc_limit(struct wuimg *img, const struct wu_conf *conf);


struct bitfield * wuimg_bitfield_init(struct wuimg *img);

struct bitfield * wuimg_bitfield_from_id(struct wuimg *img, uint16_t id);

enum wu_error wuimg_bitfield_from_mask(struct wuimg *img,
const uint32_t *mask, uint8_t ch, uint8_t word_depth);


size_t wuimg_plane_resolve(struct wuimg *img);

void wuimg_plane_cosit(struct wuimg *img, bool horz, bool vert);

void wuimg_plane_subsamp(struct wuimg *img, uint8_t horz, uint8_t vert);

struct image_planes * wuimg_plane_init(struct wuimg *img);


struct palette * wuimg_palette_set(struct wuimg *img, struct palette *pal);

struct palette * wuimg_palette_init(struct wuimg *img);

struct wu_st wuimg_palette_from_file(struct wuimg *img, uint8_t size,
size_t nmemb, FILE *ifp);

struct wu_st wuimg_palette_from_buf(struct wuimg *img, uint8_t size,
size_t nmemb, const uint8_t *src);


/* With `shown` as the currently shown frame, get the closest starting point
 * needed to render frame `i`. */
int wuimg_frame_prev_nearest(struct wuimg *img, int shown, int i);

bool wuimg_frame_set(struct wuimg *img, size_t i, size_t x, size_t y, size_t w,
size_t h, uint32_t sec_num, uint32_t sec_den, bool independent);

size_t wuimg_frames_nr(const struct wuimg *img);

struct image_frames * wuimg_frames_init(struct wuimg *img, size_t nr);


void wuimg_align(struct wuimg *img, uint8_t alignment);

bool wuimg_clone(struct wuimg *dst, struct wuimg *src);

void wuimg_free(struct wuimg *img);

void wuimg_clear(struct wuimg *img);

bool wuimg_has_data(const struct wuimg *img);

size_t wuimg_print(const struct wuimg *img, FILE *out, int verbosity);

#endif /* WUIMG */

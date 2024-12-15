// SPDX-License-Identifier: 0BSD
// Headers that jbig2.h should include but doesn't
#include <stddef.h>
#include <stdint.h>

#include <jbig2.h>

#include "misc/math.h"
#include "wudefs.h"

struct jbig2_state {
	Jbig2Ctx *ctx;
	Jbig2Image **pages;
};

static void err_fn(void *user, const char *msg, const Jbig2Severity severity,
const uint32_t seg_idx) {
	switch (severity) {
	case JBIG2_SEVERITY_DEBUG:
	case JBIG2_SEVERITY_INFO:
		break;
	case JBIG2_SEVERITY_WARNING:
	case JBIG2_SEVERITY_FATAL:
		image_file_strerror_append(user, msg);
	}
	(void)seg_idx;
}

static void jbig2_end(struct image_file *infile) {
	struct jbig2_state *ds = infile->dec_state;
	for (size_t i = 0; i < infile->nr; ++i) {
		jbig2_release_page(ds->ctx, ds->pages[i]);
	}
	free(ds->pages);
	jbig2_ctx_free(ds->ctx);
}

static enum wu_error add_image(struct image_file *infile,
const struct wu_conf *wuconf, struct jbig2_state *ds, Jbig2Image *page,
const size_t i) {
	if (zumax(page->width, page->height) > wuconf->max_img_size) {
		return wu_exceeds_size_limit;
	}

	Jbig2Image **pages = realloc(ds->pages, sizeof(*pages)*(i + 1));
	if (!pages) {
		return wu_alloc_error;
	}
	ds->pages = pages;

	struct wuimg *img = realloc_sub_images(infile, i + 1);
	if (!img) {
		return wu_alloc_error;
	}

	pages[i] = page;
	img += i;
	img->data = page->data;
	img->w = page->width;
	img->h = page->height;
	img->channels = 1;
	img->bitdepth = 1;
	img->attr = pix_inverted;
	img->borrowed = true;
	return wuimg_verify(img);
}

static enum wu_error get_images(struct image_file *infile,
const struct wu_conf *wuconf, struct jbig2_state *ds) {
	Jbig2Image *page;
	size_t i = 0;
	size_t seen = 0;
	while ((page = jbig2_page_out(ds->ctx))) {
		++seen;
		const enum wu_error st = add_image(infile, wuconf, ds, page, i);
		if (st == wu_ok) {
			++i;
		} else {
			jbig2_release_page(ds->ctx, page);
			image_file_error_append(infile, st);
		}
	}
	if (!seen) {
		return wu_no_image_data;
	}
	return i ? wu_ok : wu_decoding_error;
}

static enum wu_error jbig2_dec(struct image_file *infile,
const struct wu_conf *wuconf) {
	struct jbig2_state *ds = infile->dec_state;
	ds->ctx = jbig2_ctx_new(NULL, 0, NULL, err_fn, infile);
	if (ds->ctx) {
		if (!jbig2_data_in(ds->ctx,
		infile->map.ptr, infile->map.len)) {
			return get_images(infile, wuconf, ds);
		}
		return wu_decoding_error;
	}
	return wu_alloc_error;
}

const struct image_fn jbig2_fn = {
	.mmap = true,
	.state_size = sizeof(struct jbig2_state),
	.dec = jbig2_dec,
	.end = jbig2_end,
};

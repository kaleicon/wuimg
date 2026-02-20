// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2024 kaleido
#include <stdlib.h>

#include "misc/math.h"
#include "wudefs.h"

#include <jbig2.h>

struct jbig2_state {
	Jbig2Ctx *ctx;
	Jbig2Image **pages;
};

static void err_jbig2(void *user, const char *msg,
const Jbig2Severity severity, const uint32_t seg_idx) {
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

static void end_jbig2(struct image_file *infile) {
	struct jbig2_state *ds = infile->dec_state;
	for (size_t i = 0; i < infile->nr; ++i) {
		jbig2_release_page(ds->ctx, ds->pages[i]);
	}
	free(ds->pages);
	jbig2_ctx_free(ds->ctx);
}

static enum wu_error add_jbig2_page(struct image_file *infile,
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
	img->cs.invert = true;
	img->borrowed = true;
	return wuimg_verify(img);
}

static struct wu_st get_jbig2_pages(struct image_file *infile,
const struct wu_conf *wuconf, struct jbig2_state *ds) {
	Jbig2Image *page;
	size_t i = 0;
	size_t seen = 0;
	while ((page = jbig2_page_out(ds->ctx))) {
		++seen;
		const enum wu_error st = add_jbig2_page(infile, wuconf, ds,
			page, i);
		if (st == wu_ok) {
			++i;
		} else {
			jbig2_release_page(ds->ctx, page);
			image_file_error_append(infile, st);
		}
	}
	if (!seen) {
		return wuerr(wu_no_image_data, "no pages in file");
	}
	return i ? WU_OK : wuerr(wu_decoding_error, "no pages would be decoded");
}

static struct wu_st init_jbig2(struct image_file *infile,
const struct wu_conf *wuconf) {
	struct jbig2_state *ds = infile->dec_state;
	ds->ctx = jbig2_ctx_new(NULL, 0, NULL, err_jbig2, infile);
	if (ds->ctx) {
		if (!jbig2_data_in(ds->ctx,
		infile->map.ptr, infile->map.len)) {
			return get_jbig2_pages(infile, wuconf, ds);
		}
		return wuerr(wu_decoding_error, "jbig2_data_in() failed");
	}
	return WUERR_HERE(wu_alloc_error);
}

const struct image_fn jbig2_fn = {
	.mmap = true,
	.state_size = sizeof(struct jbig2_state),
	.init = init_jbig2,
	.end = end_jbig2,
};

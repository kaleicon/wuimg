// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2021 kaleido
#include <stdlib.h>

#include "wudefs.h"
#include "lib/xcursor.h"

static struct wu_st init_xcursor(struct image_file *infile) {
	struct xcursor_desc desc;
	struct wu_st st = xcursor_parse_header(&desc, infile->ifp, 0);
	if (!wu_isok(st)) {
		return st;
	}

	if (!alloc_sub_images(infile, desc.images)) {
		xcursor_free(&desc);
		return WUERR_HERE(wu_alloc_error);
	}

	uint32_t o = 0;
	for (uint64_t i = 0; i < desc.ntoc; ++i) {
		const struct xcursor_toc *entry = desc.toc + i;
		struct xcursor_chunk chunk;
		struct wuimg *img = NULL;
		switch (entry->type) {
		case xcursor_chunk_comment:
			st = xcursor_get_comment_info(&desc, entry, &chunk);
			if (!wu_isok(st)) {
				continue;
			}
			break;
		case xcursor_chunk_image:
			img = infile->sub_img + o;
			st = xcursor_get_image_info(&desc, entry, &chunk, img);
			if (!wu_isok(st)) {
				continue;
			}
			if (wuimg_exceeds_limit(img, infile->conf)) {
				continue;
			}
			break;
		}

		uint8_t *data = malloc(chunk.len);
		if (!data) {
			continue;
		}

		const size_t read = xcursor_get_chunk_data(&desc, &chunk, data);
		if (read) {
			switch (entry->type) {
			case xcursor_chunk_comment:
				tree_add_leaf_len(&infile->metadata,
					xcursor_comment_type_str(chunk.type),
					wuptr_mem(data, read), NULL);
				free(data);
				break;
			case xcursor_chunk_image:
				img->data = data;
				++o;
				break;
			}
		} else {
			free(data);
		}
	}
	xcursor_free(&desc);
	return WUERR_CHECK(image_file_total_decoded(infile, o));
}

const struct image_fn xcursor_fn = {
	.init = init_xcursor,
};

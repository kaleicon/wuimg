#include <stdlib.h>

#include "wudefs.h"
#include "rast_utils.h"
#include "lib/xcursor.h"

static void add_metadata(struct wu_tree *tree,
const enum xcursor_comment_type type, uint8_t *restrict data, const size_t len) {
	tree_graft_measured_leaf(tree, xcursor_comment_type_str(type),
		data, len);
}

enum wu_error xcursor_dec(struct image_file *infile,
const struct wu_conf *wuconf) {
	struct xcursor_desc desc;
	enum wu_error st = xcursor_open_file(&desc, infile->ifp);
	if (st != wu_ok) {
		return st;
	}

	st = xcursor_parse_header(&desc, 0);
	if (st != wu_ok) {
		xcursor_free(&desc);
		return st;
	}

	if (!alloc_sub_images(infile, desc.images)) {
		xcursor_free(&desc);
		return wu_alloc_error;
	}

	uint32_t o = 0;
	for (uint32_t i = 0; i < desc.ntoc; ++i) {
		const struct xcursor_toc *entry = desc.toc + i;
		struct xcursor_chunk chunk;
		struct raw_img *img = NULL;
		enum wu_error err = wu_decoding_error;
		switch (entry->type) {
		case xcursor_chunk_comment:
			err = xcursor_get_comment_info(&desc, entry, &chunk);
			if (err != wu_ok) {
				continue;
			}
			break;
		case xcursor_chunk_image:
			img = infile->sub_img + o;
			err = xcursor_get_image_info(&desc, entry, &chunk, img);
			if (err != wu_ok) {
				continue;
			}
			if (raw_img_exceeds_limit(img, wuconf)) {
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
				add_metadata(&infile->metadata,
					chunk.u.comment.type, data, read);
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
	return image_file_total_decoded(infile, o);
}

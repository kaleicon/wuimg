#include <stdlib.h>

#include "../wudefs.h"
#include "../rast_utils.h"
#include "../lib/xcursor.h"

static void add_metadata(struct wu_tree *tree,
const enum xcursor_comment_type type, uint8_t *restrict data, const size_t len) {
	tree_graft_unsafe_leaf(tree, xcursor_comment_type_str(type),
		data, len);
}

enum wu_error xcursor_dec(struct image_file *infile,
const struct wu_conf *wuconf) {
	struct xcursor_desc desc;
	enum lib_fail status = xcursor_open_file(&desc, infile->ifp);
	if (status != lib_ok) {
		return wu_invalid_signature;
	}

	status = xcursor_parse_header(&desc, 0);
	if (status != lib_ok) {
		xcursor_free(&desc);
		return wu_invalid_header;
	}

	struct raw_img *img = alloc_sub_images(infile, desc.images);
	if (!img) {
		xcursor_free(&desc);
		return wu_alloc_error;
	}

	uint32_t o = 0;
	for (uint32_t i = 0; i < desc.ntoc; ++i) {
		struct xcursor_chunk chunk;
		status = xcursor_get_chunk(&desc, &chunk, i);
		if (status != lib_ok) {
			continue;
		}

		if (chunk.type == xcursor_chunk_image
		&& rast_exceeds_size(&chunk.u.image.r, wuconf)) {
			continue;
		}

		uint8_t *data = malloc(chunk.len);
		if (!data) {
			continue;
		}

		const size_t read = xcursor_get_chunk_data(&desc, &chunk, data);
		if (read) {
			switch (chunk.type) {
			case xcursor_chunk_comment:
				add_metadata(&infile->metadata,
					chunk.u.comment.type, data, read);
				break;
			case xcursor_chunk_image:
				img[o].data = data;
				rast_to_raw(img + o, &chunk.u.image.r);
				++o;
				break;
			}
		}
	}
	xcursor_free(&desc);
	return image_file_total_decoded(infile, o);
}

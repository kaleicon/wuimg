#include <ctype.h>

#include "lib/pcx.h"
#include "misc/file.h"
#include "rast_utils.h"
#include "wudefs.h"

static size_t is_readable_garbage(const unsigned char *data, const size_t len) {
	size_t i = 0;
	while (i < len) {
		if (!data[i]) {
			break;
		} else if (!isprint(data[i])) {
			return 0;
		}
		++i;
	}
	return i;
}

static void add_metadata(const struct pcx_desc *desc, struct wuimg *img) {
	struct wu_tree *metadata = wuimg_get_metadata(img);
	if (!metadata) {
		return;
	}

	tree_add_leaf_utf8(metadata, "Format version",
		pcx_version_string(desc->version));

	struct wu_leaf leaf = {
		.val.u = img->channels,
		.type = wu_leaf_unsigned
	};
	tree_bud_leaf(metadata, "Planes", leaf);

	leaf.val.u = img->bitdepth;
	tree_bud_leaf(metadata, "Bitdepth", leaf);

	leaf.val.u = desc->palette_type;
	tree_bud_leaf(metadata, "Palette mode", leaf);

	if (desc->entries <= 4) {
		const void *garbage = desc->file_pal + 12;
		const size_t len = is_readable_garbage(garbage,
			sizeof(desc->file_pal) - 12);
		if (len > 3) {
			tree_add_leaf_len(metadata, "Garbage", garbage, len,
				NULL);
		}
	}
}

static enum wu_error common_pcx(struct pcx_desc *desc, struct wuimg *img,
const struct wu_conf *wuconf) {
	const enum wu_error st = pcx_read_header(desc, img);
	if (st == wu_ok) {
		add_metadata(desc, img);
		if (wuimg_exceeds_limit(img, wuconf)) {
			return wu_exceeds_size_limit;
		}
		return pcx_decode(desc, img);
	}
	return st;
}

static enum wu_error map_pcx(struct image_file *infile,
const struct wu_conf *wuconf, const struct map_info *mm) {
	struct pcx_desc desc;
	enum wu_error err = pcx_open_file(&desc, mp_parser_map(*mm));
	if (err == wu_ok) {
		struct wuimg *img = alloc_sub_images(infile, 1);
		err = (img) ? common_pcx(&desc, img, wuconf) : wu_alloc_error;
	}
	return err;
}

enum wu_error pcx_dec(struct image_file *infile, const struct wu_conf *wuconf) {
	return rast_map_wrap(infile, wuconf, map_pcx);
}


struct dcx_state {
	struct map_info mm;
	struct dcx_desc desc;
};

static enum wu_error get_dcx_image(struct dcx_desc *desc, struct wuimg *img,
const struct wu_conf *wuconf, const uint32_t idx) {
	struct pcx_desc pcx;
	enum wu_error st = dcx_set_file(desc, &pcx, idx);
	if (st == wu_ok) {
		return common_pcx(&pcx, img, wuconf);
	}
	return st;
}

enum wu_error dcx_callback(struct image_file *infile,
const struct wu_conf *wuconf, struct wu_state *state, const enum image_event ev) {
	struct dcx_state *ds = infile->dec_state;
	if (ev) {
		const uint32_t idx = (uint32_t)state->idx;
		struct wuimg *img = infile->sub_img + idx;
		return get_dcx_image(&ds->desc, img, wuconf, idx);
	}
	dcx_free(&ds->desc);
	file_unmap(&ds->mm);
	return wu_ok;
}

enum wu_error dcx_dec(struct image_file *infile, const struct wu_conf *wuconf) {
	struct dcx_state *ds = malloc(sizeof(*ds));
	if (!ds) {
		return wu_alloc_error;
	}

	enum wu_error st = wu_open_error;
	if (file_map(&ds->mm, infile->ifp)) {
		st = dcx_open_file(&ds->desc, mp_parser_map(ds->mm));
		if (st == wu_ok) {
			infile->dec_state = ds;
			infile->events = ev_subcycle;
			struct wuimg *img = alloc_sub_images(infile, ds->desc.nr);
			if (img) {
				return get_dcx_image(&ds->desc, img, wuconf, 0);
			}
			return wu_alloc_error;
		}
		file_unmap(&ds->mm);
	}
	free(ds);
	return st;
}

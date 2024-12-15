// SPDX-License-Identifier: 0BSD

#include <errno.h>
#include <stdio.h>

#include <sys/stat.h>

#include "misc/common.h"
#include "auto.h"
#include "fmtmap.h"

#include "dec.h"

void dec_free_image(struct image_context *image) {
	if (image->file.dec_state) {
		const struct image_fn *fn = image->desc.dec.fn;
		if (fn->end) {
			fn->end(&image->file);
		}
		if (fn->state_size) {
			free(image->file.dec_state);
		}
	}
	image_file_free(&image->file);
}

enum wu_error dec_callback(struct image_context *image,
enum image_event event) {
	struct image_file *infile = &image->file;
	struct wu_state *state = &image->state;
	event &= image_cur_events(image);
	switch (event) {
	case ev_none:
		return wu_no_change;
	case ev_subcycle:
		if (infile->sub_img[state->idx].data) {
			return wu_no_change;
		}
		break;
	default:
		break;
	}
	const struct fmt_desc *desc = &image->desc;
	const enum wu_error status = desc->is_auto
		? auto_load(infile, desc->dec.desc)
		: desc->dec.fn->callback(infile, &image->conf, state, event);
	if (status == wu_ok) {
		image_file_normalize(infile);
	}
	return status;
}

static enum wu_error init_metadata(struct image_file *infile,
const struct fmt_desc *fmt, const int fd) {
	struct wu_tree *metadata = &infile->metadata;
	if (!tree_sow(metadata, "Metadata")) {
		return wu_alloc_error;
	}

	if (fmt) {
		tree_add_leaf_utf8_limit(metadata, "Format",
			WUPTR_ARRAY(fmt->name));
	}

	struct stat sb;
	if (fstat(fd, &sb)) {
		return wu_ok;
	}

	struct wu_tree *fdmeta = tree_add_branch(metadata, "Stat");
	if (!fdmeta) {
		return wu_alloc_error;
	}

	const struct wu_tree_sap sap[] = {
		{"Size", {wu_leaf_signed, {.d = sb.st_size}}},
		{"Last access", {wu_leaf_time, {.time = sb.st_atim.tv_sec}}},
		{"Last modified", {wu_leaf_time, {.time = sb.st_mtim.tv_sec}}},
		{"Last status change", {wu_leaf_time,
			{.time = sb.st_ctim.tv_sec}}},
	};
	tree_bud_leaves(fdmeta, sap, infile->stat ? ARRAY_LEN(sap) : 1);
	return wu_ok;
}

static void errno_append(struct image_file *infile, const int n) {
	if (n) {
		image_file_strerror_append(infile, strerror(n));
	}
}

static enum wu_error actually_open(struct image_context *image) {
	struct image_file *infile = &image->file;
	struct wuptr *map = &infile->map;
	if (!infile->ifp && !map->ptr) {
		if (!image->name) {
			fatal_bug("dec_decode()",
				"No data source for image_context");
		}
		errno = 0;
		infile->ifp = fopen(image->name, "rb");
		if (!infile->ifp) {
			errno_append(infile, errno);
			return wu_open_error;
		}
	}

	if (!image->desc.dec.fn) {
		errno = 0;
		const struct fmt_desc *fmt = fmtmap_identify(image);
		if (!fmt) {
			errno_append(infile, errno);
			return wu_unknown_file_type;
		}
		image->desc = *fmt;
	}

	int fd = -1;
	if (infile->ifp) {
		fd = fileno(infile->ifp);
		if (!image->desc.is_auto && image->desc.dec.fn->mmap) {
			if (!map->ptr) {
				errno = 0;
				if (!file_map_fd(map, fd)) {
					errno_append(infile, errno);
					return wu_open_error;
				}
			}
		} else {
			rewind(infile->ifp);
			fflush(infile->ifp); /* tmpfiles require this */
		}
	} else {
		if (image->desc.is_auto || !image->desc.dec.fn->mmap) {
			errno = 0;
			infile->ifp = fmemopen((uint8_t *)map->ptr, map->len,
				"rb");
			if (!infile->ifp) {
				errno_append(infile, errno);
				return wu_alloc_error; // ???
			}
		}
	}
	return init_metadata(infile, &image->desc, fd);
}

enum wu_error dec_decode(struct image_context *image) {
	enum wu_error st = actually_open(image);
	if (st == wu_ok) {
		struct image_file *infile = &image->file;
		const struct wu_conf *conf = &image->conf;
		const struct fmt_desc *desc = &image->desc;
		if (desc->is_auto || desc->dec.fn->alloc_single) {
			if (!alloc_sub_images(infile, 1)) {
				return wu_alloc_error;
			}
		}
		if (!desc->is_auto && desc->dec.fn->state_size) {
			infile->dec_state = calloc(1, desc->dec.fn->state_size);
			if (!infile->dec_state) {
				return wu_alloc_error;
			}
		}
		st = desc->is_auto
			? auto_init(infile, conf, desc->dec.desc)
			: desc->dec.fn->dec(infile, conf);
		if (st == wu_ok) {
			image_file_normalize(infile);
			if (!infile->sub_img->data) {
				st = dec_callback(image, ev_subcycle);
				if (st == wu_no_change) {
					st = wu_ok;
				}
			}
		}
	}
	return st;
}

enum wu_error dec_iter(struct image_context *image,
struct wuimg **cur_img) {
	enum wu_error err;
	struct wu_state *state = &image->state;
	if (!image->file.nr) {
		err = dec_decode(image);
		*cur_img = image->file.sub_img;
		return err;
	}

	enum image_event ev = 0;
	const struct wuimg *img = image->file.sub_img + state->idx;
	const int frames = (int)wuimg_frames_nr(img);
	if (state->frame + 1 < frames) {
		ev = ev_frame;
		++state->frame;
	} else if (state->idx + 1 < (int)image->file.nr) {
		ev = ev_subcycle;
		++state->idx;
		state->frame = 0;
	}

	if (ev) {
		err = dec_callback(image, ev);
		if (err == wu_ok || err == wu_no_change) {
			*cur_img = image->file.sub_img + state->idx;
			err = wu_ok;
		}
		return err;
	}
	return wu_no_change;
}

void dec_src_mem(struct image_context *image, const struct wuptr data,
const char *name, const struct image_fn *fn) {
	image->name = name;
	image->file.map = data;
	image->file.keep_map = true;
	image->desc.dec.fn = fn;
}

void dec_src_file(struct image_context *image, FILE *ifp, const char *name,
const bool keep_file, const bool stat_file) {
	image->name = name;
	image->file.ifp = ifp;
	image->file.keep_file = keep_file;
	image->file.stat = stat_file;
}

void dec_src_filename(struct image_context *image, const char *filename) {
	image->name = filename;
	image->file.stat = true;
}

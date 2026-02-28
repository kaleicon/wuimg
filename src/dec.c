// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2019 kaleido
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <sys/stat.h>

#include "misc/common.h"
#include "misc/file.h"
#include "misc/math.h"
#include "auto.h"
#include "fmtmap.h"

#include "dec.h"

struct wuimg * wudec_cur_sub_img(const struct wudec_image *image) {
	return image->file.sub_img + image->state.idx;
}

bool wudec_cur_is_anim(const struct wudec_image *image) {
	const struct wuimg *img = wudec_cur_sub_img(image);
	return img->evolving || img->frames;
}

enum image_event wudec_cur_events(const struct wudec_image *image) {
	const struct wuimg *img = wudec_cur_sub_img(image);
	return ev_subcycle
		| (img->evolving ? ev_time : 0)
		| (img->scalable ? ev_transform : 0)
		| (img->frames ? ev_frame : 0);
}

enum image_event wudec_zoom(struct wudec_image *image, float new_zoom) {
	const float max = exp2f(WU_SCALING_POW);
	const float min = exp2f(-WU_SCALING_POW);

	new_zoom = fclampf(new_zoom, min, max);
	if (new_zoom != image->state.zoom) {
		image->state.zoom = new_zoom;
		return ev_transform;
	}
	return 0;
}

enum image_event wudec_sub_cycle(struct wudec_image *image, int steps) {
	const int c = imod(image->state.idx + steps, (int)image->file.nr);
	if (c != image->state.idx) {
		image->state.idx = c;
		return ev_subcycle;
	}
	return 0;
}

enum image_event wudec_frame_cycle(struct wudec_image *image, int steps) {
	const struct image_frames *frames = wudec_cur_sub_img(image)->frames;
	if (frames) {
		const int f = imod(image->state.frame + steps, (int)frames->nr);
		if (f != image->state.frame) {
			image->state.frame = f;
			return ev_frame;
		}
	}
	return 0;
}


void wudec_free(struct wudec_image *image) {
	/* dec_state must be freed first, as some formats may use this
	 * interface to decode an embedded file. */
	if (image->file.dec_state && !image->desc.is_auto) {
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

static void recycle_base(struct wudec_image *image) {
	wudec_free(image);
	image->file = (struct image_file){0};
	image->desc = (struct fmt_desc){0};
}

void wudec_recycle_state(struct wudec_image *image) {
	recycle_base(image);
	image->state.idx = 0;
	image->state.frame = 0;
	image->state.time = 0;
}

void wudec_recycle_conf(struct wudec_image *image) {
	recycle_base(image);
	image->state = (struct wu_state){0};
}

static enum wu_error call_event(struct wudec_image *image,
const enum image_event event) {
	const struct fmt_desc *desc = &image->desc;
	const struct wu_st st = (desc->is_auto)
		? auto_load(&image->file)
		: desc->dec.fn->event(&image->file, &image->state, event);
	if (st.msg) {
		image_file_strerror_append(&image->file, st.msg);
	}
	return st.st;
}

enum wu_error wudec_callback(struct wudec_image *image,
enum image_event event) {
	struct image_file *infile = &image->file;
	struct wu_state *state = &image->state;
	struct wuimg *img = infile->sub_img + state->idx;
	event &= wudec_cur_events(image);
	switch (event) {
	case ev_none:
		break;
	case ev_subcycle:
		if (img->data) {
			break;
		} else if (!image->desc.is_auto) {
			enum wu_error e = call_event(image, ev_metadata);
			if (e != wu_ok && e != wu_no_change) {
				return e;
			}
			if (image->desc.dec.fn->alloc_on_subcycle
			&& !img->borrowed) {
				e = wuimg_alloc_limit(img, image->file.conf);
				if (e != wu_ok) {
					return e;
				}
			}
		}
		// fallthrough
	case ev_metadata:
	case ev_frame:
	case ev_time:
	case ev_transform:
		return call_event(image, event);
	}
	return wu_no_change;
}

static enum wu_error init_metadata(struct image_file *infile,
const struct fmt_desc *fmt, const int fd) {
	struct wutree *metadata = &infile->metadata;
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

	struct wutree *fdmeta = tree_add_branch(metadata, "Stat");
	if (!fdmeta) {
		return wu_alloc_error;
	}

	const struct wutree_sap sap[] = {
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

static enum wu_error actually_open(struct wudec_image *image) {
	struct image_file *infile = &image->file;
	infile->conf = &image->conf;
	struct wuptr *map = &infile->map;
	long offset = 0;
	if (infile->ifp) {
		offset = ftell(infile->ifp);
	} else if (!map->ptr) {
		if (!infile->name) {
			fatal_bug("wudec_decode()",
				"No data source for image_context");
		}
		errno = 0;
		infile->ifp = fopen(infile->name, "rb");
		if (!infile->ifp) {
			errno_append(infile, errno);
			return wu_open_error;
		}
	}

	if (!image->desc.dec.fn) {
		errno = 0;
		const struct fmt_desc *fmt = fmtmap_identify(infile,
			infile->name);
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
			fseek(infile->ifp, offset, SEEK_SET);
			// propagate seek to file descriptor. needed for tiff
			fflush(infile->ifp);
		}
	} else {
		if (image->desc.is_auto || !image->desc.dec.fn->mmap) {
			errno = 0;
			infile->ifp = fmemopen((uint8_t *)map->ptr, map->len,
				"rb");
			if (!infile->ifp) {
				errno_append(infile, errno);
				return wu_alloc_error;
			}
		}
	}
	return init_metadata(infile, &image->desc, fd);
}

static enum wu_error call_decoder(struct image_file *infile,
const struct fmt_desc *desc) {
	struct wu_st st = wuerr(wu_invalid_params, "no decoding function!");
	if (desc->is_auto) {
		st = auto_init(infile, *desc->dec.desc);
	} else if (desc->dec.fn->init) {
		st = desc->dec.fn->init(infile);
	} else if (desc->dec.fn->alloc_single) {
		return wu_ok;
	}
	if (st.msg) {
		image_file_strerror_append(infile, st.msg);
	}
	return st.st;
}

enum wu_error wudec_decode(struct wudec_image *image) {
	enum wu_error st = actually_open(image);
	if (st == wu_ok) {
		struct image_file *infile = &image->file;
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
		st = call_decoder(infile, desc);
		if (st == wu_ok) {
			if (!infile->nr) {
				image_file_strerror_append(infile,
					"No sub-images");
				return wu_no_image_data;
			} else if (!infile->sub_img
			&& !alloc_sub_images(infile, infile->nr)) {
				image_file_strerror_append(infile,
					"Failed to allocate sub-images in"
					" wudec_decode()");
				return wu_alloc_error;
			} else if (!infile->sub_img->data) {
				st = wudec_callback(image, ev_subcycle);
				if (st == wu_no_change) {
					fatal_bug(__func__,
						"Callback returned `no change`"
						" for sub-image request");
				}
			}
		} else if (infile->nr && !infile->sub_img) {
			infile->nr = 0;
		}
	}
	return st;
}

struct wu_st wudec_decode_embedded(struct image_file *infile,
struct wuimg *img, struct wudec_image *src) {
	enum wu_error err = wudec_decode(src);
	if (err == wu_ok) {
		struct wutree *metadata = img->metadata; // may be null
		struct wuimg *src_img = src->file.sub_img;
		struct wutree *src_meta = src_img->metadata;
		if (src_meta) {
			metadata = wuimg_get_metadata(img);
			if (!metadata
			|| !tree_graft_branch(metadata, src_meta, "Embedded")) {
				tree_unroot(src_meta);
			}
			free(src_meta);
		}
		*img = *src_img;
		img->metadata = metadata;
		*src_img = (struct wuimg){0};
	}
	wustr_append_wustr(&infile->errors, &src->file.errors);
	return wuerr(err, err == wu_ok ? NULL : "failed to decode embedded file");
}

enum wu_error wudec_iter(struct wudec_image *image,
struct wuimg **cur_img) {
	enum wu_error err;
	struct wu_state *state = &image->state;
	if (!image->file.nr) {
		err = wudec_decode(image);
		*cur_img = image->file.sub_img;
		return err;
	}

	enum image_event ev = 0;
	struct wuimg *img = image->file.sub_img + state->idx;
	const int frames = (int)wuimg_frames_nr(img);
	if (state->frame + 1 < frames) {
		ev = ev_frame;
		++state->frame;
	} else if (state->idx + 1 < (int)image->file.nr) {
		ev = ev_subcycle;
		++state->idx;
		state->frame = 0;
		if (!img->borrowed) {
			wuimg_clear(img);
		}
	}

	if (ev) {
		err = wudec_callback(image, ev);
		if (err == wu_ok || err == wu_no_change) {
			*cur_img = image->file.sub_img + state->idx;
			err = wu_ok;
		}
		return err;
	}
	return wu_no_change;
}


void wudec_src_format(struct wudec_image *image, const struct fmt_desc *fmt) {
	if (fmt) {
		image->desc = *fmt;
	}
}

void wudec_src_dec_fn(struct wudec_image *image, const struct image_fn *fn) {
	image->desc = (struct fmt_desc) {
		.dec.fn = fn,
	};
}

void wudec_src_auto_desc(struct wudec_image *image, const struct wuptr *desc) {
	image->desc = (struct fmt_desc) {
		.is_auto = true,
		.dec.desc = desc,
	};
}

void wudec_src_mem(struct wudec_image *image, const struct wuptr data,
const char *name) {
	image->file.name = name;
	image->file.map = data;
	image->file.keep_map = true;
}

void wudec_src_file(struct wudec_image *image, FILE *ifp, const char *name,
const bool keep_file, const bool stat_file) {
	image->file.name = name;
	image->file.ifp = ifp;
	image->file.keep_file = keep_file;
	image->file.stat = stat_file;
}

void wudec_src_filename(struct wudec_image *image, const char *filename) {
	image->file.name = filename;
	image->file.stat = true;
}

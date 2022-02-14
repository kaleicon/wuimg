#include <stdio.h>
#include <string.h>
#include <errno.h>

#include <sys/stat.h>

#include "wudefs.h"
#include "common.h"
#include "dec.h"

// Generated files
#include "dec_enable.def"
#include "dec_include.def"
#include "dec_fmtmap.h"

typedef enum wu_error (*dec_func_t)(struct image_file *infile,
	const struct wu_conf *wuconf);

typedef enum wu_error (*dec_callback_t)(struct image_file *infile,
	const struct wu_conf *wuconf, struct wu_state *state,
	enum image_event);


struct fmt_fn {
	const dec_func_t dec;
	const dec_callback_t callback;
	const char name[8];
};

static const struct fmt_fn format_map[] = {
#define WUDEC(name, callback) {name##_dec, callback, #name},
#include "dec.def"
#undef WUDEC
};

enum wu_error dec_callback_image(struct image_context *image,
const enum image_event event) {
	struct image_file *infile = &image->file;
	const enum image_event ev = infile->events & event;
	enum wu_error status = wu_no_change;
	if (ev || !event) {
		const int id = image->fmt_id;
		status = format_map[id].callback(infile, &image->conf,
			&image->state, ev);
		if (status == wu_ok) {
			image_file_normalize(infile);
		}
	}
	return status;
}

void dec_free_image(struct image_context *image) {
	if (image->file.dec_state) {
		format_map[image->fmt_id].callback(&image->file, &image->conf,
			&image->state, 0);
	}
	image_file_free(&image->file);
}

static void stat_metadata(struct wu_tree *tree, const int fd) {
	struct stat sb;
	if (fstat(fd, &sb) != 0) {
		return;
	}

	struct wu_tree *meta = tree_sprout_branch(tree, "Stats");
	if (!meta) {
		return;
	}

	const struct wu_tree_sap sap[] = {
		{"Size", wu_leaf_signed, {.d = sb.st_size}},
		{"Last access", wu_leaf_time, {.time = sb.st_atim.tv_sec}},
		{"Last modified", wu_leaf_time, {.time = sb.st_mtim.tv_sec}},
		{"Last status change", wu_leaf_time,
			{.time = sb.st_ctim.tv_sec}},
	};
	tree_bud_leaves(meta, sap, ARRAY_LEN(sap));
}

enum wu_error dec_decode_image(struct image_context *image) {
	struct image_file *infile = &image->file;
	if (!infile->ifp) {
		errno = 0;
		infile->ifp = fopen(image->name, "rb");
		if (!infile->ifp) {
			if (errno) {
				image_file_error_append(infile, strerror(errno));
			}
			return wu_open_error;
		}
	}

	errno = 0;
	const int id = fmtmap_identify_file(infile->ifp, image->name);
	if (id == -1) {
		if (errno) {
			image_file_error_append(infile, strerror(errno));
			return wu_open_error;
		}
		return wu_unknown_file_type;
	}

	struct wu_tree *metadata = &infile->metadata;
	if (!tree_sow(metadata, "Metadata")) {
		return wu_alloc_error;
	}
	tree_sprout_leaf(metadata, "Format", format_map[id].name);
	stat_metadata(metadata, fileno(infile->ifp));

	const enum wu_error result = format_map[id].dec(infile, &image->conf);
	if (result == wu_ok) {
		image->fmt_id = id;
		image_file_normalize(infile);
	}
	return result;
}

enum wu_error dec_iter_image(struct image_context *image,
const struct raw_img **cur_img) {
	enum wu_error err;
	struct wu_state *state = &image->state;
	if (!image->file.nr) {
		err = dec_decode_image(image);
		*cur_img = image->file.sub_img;
		return err;
	}

	enum image_event ev = 0;
	const struct raw_img *img = image->file.sub_img + state->idx;
	const int frames = img->frames ? (int)img->frames->nr : 1;
	if (state->frame + 1 < frames) {
		ev = ev_frame;
		++state->frame;
	} else if (state->idx + 1 < (int)image->file.nr) {
		ev = ev_subcycle;
		++state->idx;
		state->frame = 0;
	}

	if (ev) {
		err = dec_callback_image(image, ev);
		if (err == wu_ok) {
			*cur_img = image->file.sub_img + state->idx;
		}
		return err;
	}
	return wu_no_change;
}

void print_known_formats(void) {
	printf("Known formats: %zu\n", ARRAY_LEN(format_map));
	for (size_t i = 0; i < ARRAY_LEN(format_map); ++i) {
		const struct fmt_fn *f = format_map + i;
		fwrite(f->name, 1, zumin(sizeof(f->name), strlen(f->name)),
			stdout);
		const char *sep = (i + 1 < ARRAY_LEN(format_map))
			? ", " : "\n\n";
		fputs(sep, stdout);
	}
	fmtmap_print_data();
}

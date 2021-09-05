#include <stdio.h>
#include <errno.h>

#include <sys/stat.h>

#include "wudefs.h"
#include "common.h"
#include "dec.h"

// Semi-generated files
#include "dec_includes.h" /* enabled file formats */
#include "dec_fmtmap.h" /* output of dec_fmtmap_sort_quine.c */

typedef enum wu_error (*dec_func_t)(struct image_file *infile,
	const struct wu_conf *wuconf);

typedef enum wu_error (*dec_callback_t)(struct image_file *infile,
	const struct wu_conf *wuconf, struct wu_state *state,
	enum image_event);


struct format_fn {
	const dec_func_t dec;
	const dec_callback_t callback;
	const char name[8];
};

static const struct format_fn format_map[] = {
#define WUDEC(name, callback) {name##_dec, callback, #name},
#include "dec.def"
#undef WUDEC
};

enum wu_error callback_image(struct image_context *image,
const enum image_event event) {
	struct image_file *infile = &image->file;
	const enum image_event ev = infile->events & event;
	enum wu_error status = wu_no_change;
	if (ev || !event) {
		const enum format_id id = image->fmt_id;
		status = format_map[id].callback(infile, &image->conf,
			&image->state, ev);
		if (status == wu_ok) {
			image_file_normalize(infile);
		}
	}
	return status;
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

enum wu_error decode_image(struct image_context *image) {
	struct image_file *infile = &image->file;
	if (!infile->ifp) {
		errno = 0;
		infile->ifp = fopen(image->name, "rb");
		if (!infile->ifp) {
			if (errno) {
				infile->err_msg = (strerror_dup(errno));
			}
			return wu_open_error;
		}
	}

	errno = 0;
	const enum format_id id = identify_image(infile->ifp, image->name);
	if (id == fmt_unknown) {
		if (errno) {
			infile->err_msg = strerror_dup(errno);
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

void print_known_formats(void) {
	printf("Known formats: %zu\n", ARRAY_LEN(format_map));
	for (size_t i = 0; i < ARRAY_LEN(format_map); ++i) {
		fputs(format_map[i].name, stdout);
		if (i + 1 < ARRAY_LEN(format_map)) {
			fputs(", ", stdout);
		} else {
			fputs("\n\n", stdout);
		}
	}

	print_map_data();
}

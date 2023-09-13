// SPDX-License-Identifier: 0BSD
#ifndef WU_FILESYSTEM
#define WU_FILESYSTEM

#include "misc/wustr.h"

struct fs_path {
	struct wustr parent;
	struct wuptr file;
};

void fs_path_free(struct fs_path *path);

void fs_path_set_file(struct fs_path *path, const struct wuptr name);

int fs_get_parent_dir(struct fs_path *path, const char *str, bool must_exist);

char ** fs_filter_sort(const char *name, size_t *nr, size_t *start_idx);

#endif /* WU_FILESYSTEM */

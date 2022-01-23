#ifndef WU_FILESYSTEM
#define WU_FILESYSTEM

#include "wustr.h"

struct fs_path {
	struct wustr parent;
	struct wuptr file;
};

void fs_path_free(struct fs_path *path);

int fs_get_parent_dir(const char *str, struct fs_path *path);

char ** fs_filter_sort(const char *name, size_t *nr, size_t *start_idx);

#endif /* WU_FILESYSTEM */
